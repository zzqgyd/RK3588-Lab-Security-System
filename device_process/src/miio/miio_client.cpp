// ================================================================
// miio 协议客户端实现
// ----------------------------------------------------------------
// 严格按照 python-miio 的 miio/protocol.py 实现
// 参考：https://github.com/rytilahti/python-miio/blob/master/miio/protocol.py
// ================================================================

#include "miio/miio_client.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <ctime>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <chrono>
#include <thread>

#include <openssl/evp.h>
#include <openssl/md5.h>

namespace miio {

// ================================================================
// 构造 / 析构
// ================================================================
MiioClient::MiioClient(const std::string& ip, uint16_t port, const std::string& token_hex)
    : ip_(ip), port_(port), sock_fd_(-1),
      connected_(false), did_(0), device_ts_(0), msg_id_(1)
{
    auto bytes = hexToBytes(token_hex);
    if (bytes.size() != 16) {
        throw std::invalid_argument("token 必须是 32 位 hex 字符串");
    }
    memcpy(token_, bytes.data(), 16);

    sock_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd_ < 0) {
        throw std::runtime_error("socket() 失败");
    }
}

MiioClient::~MiioClient()
{
    if (sock_fd_ >= 0) close(sock_fd_);
}

// ================================================================
// connect：发 Hello 包握手
// ----------------------------------------------------------------
// Hello 包结构（32 字节，无 payload）：
//   21 31 00 20              magic + length=32
//   00 00 00 00              unknown=0
//   00 00 00 00              device_id=0
//   <ts:4B>                  当前时间戳
//   00 00 00 00 00 00 00 00  checksum=0（hello 不计算）
//   00 00 00 00 00 00 00 00  checksum 续
// ================================================================
bool MiioClient::connect()
{
    std::lock_guard<std::mutex> lock(mutex_);

    // Hello 包（按抓包 python-miio 实际行为）：
    //   offset 0-1:  21 31           magic
    //   offset 2-3:  00 20           length=32
    //   offset 4-31: 全部 0xFF（共28字节）
    std::vector<uint8_t> hello(32, 0xFF);
    hello[0] = 0x21; hello[1] = 0x31;
    hello[2] = 0x00; hello[3] = 0x20;

    printf("[MiioClient] 发送 Hello 包到 %s:%u\n", ip_.c_str(), port_);

    std::vector<uint8_t> resp;
    if (!sendAndReceive(hello, resp, 3000)) {
        fprintf(stderr, "[MiioClient] Hello 无响应\n");
        connected_ = false;
        return false;
    }

    if (resp.size() < 16) {
        fprintf(stderr, "[MiioClient] Hello 响应太短\n");
        connected_ = false;
        return false;
    }

    uint16_t magic = (resp[0] << 8) | resp[1];
    if (magic != 0x2131) {
        fprintf(stderr, "[MiioClient] 魔数错误: 0x%04X\n", magic);
        connected_ = false;
        return false;
    }

    // Hello 响应字段：
    //   offset 8-11:  device_id（设备真实 did）
    //   offset 12-15: ts（设备 uptime，秒，非 Unix 时间戳！）
    did_ = ((uint32_t)resp[8]  << 24) | ((uint32_t)resp[9]  << 16)
          | ((uint32_t)resp[10] << 8)  | (uint32_t)resp[11];

    // 关键：ts 是设备 uptime（从启动开始秒数），不是 Unix 时间戳
    // 设备会校验 RPC 请求的 ts 是否接近自身时钟，差距太大会直接丢包
    device_ts_ = ((uint32_t)resp[12] << 24) | ((uint32_t)resp[13] << 16)
               | ((uint32_t)resp[14] << 8)  | (uint32_t)resp[15];

    printf("[MiioClient] 握手成功: did=0x%08X, device_ts=%u\n", did_, device_ts_);

    connected_ = (did_ != 0);
    return connected_;
}

// ================================================================
// rpcCall
// ================================================================
bool MiioClient::rpcCall(const std::string& method,
                        const nlohmann::json& params,
                        nlohmann::json& response,
                        int timeout_ms)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) return false;

    nlohmann::json req = {
        {"id", msg_id_++},
        {"method", method},
        {"params", params}
    };
    std::string payload = req.dump();
    auto packet = buildRpcPacket(payload);

    // 最多重试 3 次：设备硬件 ack 可能超时（-9999），需更长等待
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::vector<uint8_t> recv_buf;
        if (!sendAndReceive(packet, recv_buf, timeout_ms)) {
            if (attempt < 2) {
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                continue;
            }
            return false;
        }

        std::string json_str;
        if (!parseResponse(recv_buf, json_str)) {
            if (attempt < 2) continue;
            return false;
        }

        try {
            response = nlohmann::json::parse(json_str);
        } catch (...) {
            fprintf(stderr, "[MiioClient] JSON 解析失败: %s\n", json_str.c_str());
            return false;
        }

        if (response.contains("error")) {
            int code = response["error"].value("code", 0);
            // -9999 = user ack timeout，设备忙或硬件响应慢，延迟后重试
            if (code == -9999 && attempt < 2) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
            fprintf(stderr, "[MiioClient] RPC 错误: %s\n", response["error"].dump().c_str());
            return false;
        }

        return true;
    }
    return false;
}

// ================================================================
// getProperties
// ================================================================
bool MiioClient::getProperties(const std::vector<std::pair<int,int>>& props,
                              nlohmann::json& values,
                              int timeout_ms)
{
    nlohmann::json params = nlohmann::json::array();
    for (auto& p : props) {
        params.push_back({{"siid", p.first}, {"piid", p.second}});
    }

    nlohmann::json resp;
    if (!rpcCall("get_properties", params, resp, timeout_ms)) return false;

    values = resp.value("result", nlohmann::json::array());
    return true;
}

// ================================================================
// setProperties
// ================================================================
bool MiioClient::setProperties(const nlohmann::json& props, int timeout_ms)
{
    nlohmann::json resp;
    if (!rpcCall("set_properties", props, resp, timeout_ms)) return false;

    auto result = resp.value("result", nlohmann::json::array());
    if (!result.is_array()) return false;
    for (auto& item : result) {
        if (item.value("code", -1) != 0) return false;
    }
    return true;
}

// ================================================================
// callAction
// ================================================================
bool MiioClient::callAction(int siid, int aiid,
                           const nlohmann::json& in_params,
                           nlohmann::json& out_params,
                           int timeout_ms)
{
    // 关键：in 字段必须是数组，不能是 null（设备不认 in:null）
    nlohmann::json in_arr = in_params.is_null() ? nlohmann::json::array() : in_params;

    nlohmann::json params = nlohmann::json::array({
        {
            {"did", std::to_string(did_)},
            {"siid", siid},
            {"aiid", aiid},
            {"in", in_arr}
        }
    });

    nlohmann::json resp;
    if (!rpcCall("action", params, resp, timeout_ms)) return false;

    auto result = resp.value("result", nlohmann::json::array());
    if (!result.is_array() || result.empty()) return false;
    if (result[0].value("code", -1) != 0) return false;

    out_params = result[0].value("out", nlohmann::json::array());
    return true;
}

// ================================================================
// 底层收发
// ================================================================
bool MiioClient::sendAndReceive(const std::vector<uint8_t>& send_buf,
                               std::vector<uint8_t>& recv_buf,
                               int timeout_ms)
{
    if (sock_fd_ < 0) return false;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, ip_.c_str(), &addr.sin_addr) <= 0) return false;

    // 发送前清空 socket 接收缓冲区，避免收到上一个请求的延迟响应
    struct pollfd drain;
    drain.fd = sock_fd_;
    drain.events = POLLIN;
    uint8_t tmp[4096];
    while (poll(&drain, 1, 0) > 0) {
        recvfrom(sock_fd_, tmp, sizeof(tmp), 0, nullptr, nullptr);
    }

    ssize_t n = sendto(sock_fd_, send_buf.data(), send_buf.size(), 0,
                       (struct sockaddr*)&addr, sizeof(addr));
    if (n != (ssize_t)send_buf.size()) return false;

    struct pollfd pfd;
    pfd.fd = sock_fd_;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret <= 0) return false;

    recv_buf.resize(4096);
    n = recvfrom(sock_fd_, recv_buf.data(), recv_buf.size(), 0, nullptr, nullptr);
    if (n <= 0) return false;
    recv_buf.resize(n);
    return true;
}

// ================================================================
// RPC 包
// ----------------------------------------------------------------
// 严格按 python-miio 的 Message Struct：
//   header (16B) + checksum (16B) + ciphertext
//
//   header: magic(2) + length(2) + unknown(4) + device_id(4) + ts(4)
//   length = 32 + ciphertext.length
//   checksum = md5(header_16B + token_16B + ciphertext)
// ================================================================
std::vector<uint8_t> MiioClient::buildRpcPacket(const std::string& json_payload)
{
    // python-miio 加密前追加 \x00
    std::vector<uint8_t> plaintext(json_payload.begin(), json_payload.end());
    plaintext.push_back(0x00);

    auto ciphertext = aesEncrypt(plaintext);

    // 关键：ts 用设备 uptime（每次 RPC 递增 1），不是 Unix 时间戳
    // 设备会校验 ts 接近自身时钟，Unix 时间戳会导致设备直接丢包
    uint32_t ts = ++device_ts_;
    uint16_t total_len = 32 + (uint16_t)ciphertext.size();

    // 构造前 16 字节头
    std::vector<uint8_t> header(16);
    header[0]  = 0x21; header[1]  = 0x31;
    header[2]  = (total_len >> 8) & 0xFF;
    header[3]  = total_len & 0xFF;
    header[4]  = 0x00; header[5]  = 0x00;     // unknown
    header[6]  = 0x00; header[7]  = 0x00;
    header[8]  = (did_ >> 24) & 0xFF;
    header[9]  = (did_ >> 16) & 0xFF;
    header[10] = (did_ >> 8)  & 0xFF;
    header[11] = did_ & 0xFF;
    header[12] = (ts >> 24) & 0xFF;
    header[13] = (ts >> 16) & 0xFF;
    header[14] = (ts >> 8)  & 0xFF;
    header[15] = ts & 0xFF;

    // checksum = md5(header + token + ciphertext)
    std::vector<uint8_t> md5_input;
    md5_input.insert(md5_input.end(), header.begin(), header.end());
    md5_input.insert(md5_input.end(), token_, token_ + 16);
    md5_input.insert(md5_input.end(), ciphertext.begin(), ciphertext.end());
    auto checksum = md5(md5_input.data(), md5_input.size());

    // 组装：header + checksum + ciphertext
    std::vector<uint8_t> pkt;
    pkt.reserve(32 + ciphertext.size());
    pkt.insert(pkt.end(), header.begin(), header.end());
    pkt.insert(pkt.end(), checksum.begin(), checksum.end());
    pkt.insert(pkt.end(), ciphertext.begin(), ciphertext.end());
    return pkt;
}

// ================================================================
// 解析响应包
// ----------------------------------------------------------------
// 响应结构同请求：header(16) + checksum(16) + ciphertext
// 解密后 rstrip \x00
// ================================================================
bool MiioClient::parseResponse(const std::vector<uint8_t>& buf, std::string& json_out)
{
    if (buf.size() < 32) {
        fprintf(stderr, "[MiioClient] 响应太短: %zu bytes\n", buf.size());
        return false;
    }

    uint16_t magic = (buf[0] << 8) | buf[1];
    if (magic != 0x2131) {
        fprintf(stderr, "[MiioClient] 响应魔数错误: 0x%04X\n", magic);
        return false;
    }

    // 关键：每次收到响应都从包头 offset 12-15 提取设备真实 ts
    // 设备 uptime 在持续增长，自增的 ts 会逐渐落后，必须同步更新
    // 否则差距过大后设备会认为是无效包直接丢包，导致连续失败 → 离线
    uint32_t resp_ts = ((uint32_t)buf[12] << 24) | ((uint32_t)buf[13] << 16)
                     | ((uint32_t)buf[14] << 8)  | (uint32_t)buf[15];
    if (resp_ts > device_ts_) {
        device_ts_ = resp_ts;
    }

    if (buf.size() <= 32) return false;
    std::vector<uint8_t> ciphertext(buf.begin() + 32, buf.end());

    auto plaintext = aesDecrypt(ciphertext);
    if (plaintext.empty()) {
        fprintf(stderr, "[MiioClient] AES 解密失败\n");
        return false;
    }

    // python-miio：decrypted.rstrip(b"\x00")
    while (!plaintext.empty() && plaintext.back() == 0x00) {
        plaintext.pop_back();
    }

    json_out.assign((char*)plaintext.data(), plaintext.size());
    return true;
}

// ================================================================
// key/iv 派生（python-miio 的 key_iv 函数）
// ----------------------------------------------------------------
//   key = md5(token)
//   iv  = md5(key + token)
// ================================================================
void MiioClient::deriveKeyIv(uint8_t key[16], uint8_t iv[16]) const
{
    auto key_digest = md5(token_, 16);
    memcpy(key, key_digest.data(), 16);

    std::vector<uint8_t> iv_input;
    iv_input.insert(iv_input.end(), key_digest.begin(), key_digest.end());
    iv_input.insert(iv_input.end(), token_, token_ + 16);
    auto iv_digest = md5(iv_input.data(), iv_input.size());
    memcpy(iv, iv_digest.data(), 16);
}

// ================================================================
// AES-128-CBC + PKCS7 加密
// ================================================================
std::vector<uint8_t> MiioClient::aesEncrypt(const std::vector<uint8_t>& plaintext) const
{
    uint8_t key[16], iv[16];
    deriveKeyIv(key, iv);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    std::vector<uint8_t> out(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
    int outlen = 0, total = 0;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr, key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx); return {};
    }
    EVP_CIPHER_CTX_set_padding(ctx, 1);

    if (EVP_EncryptUpdate(ctx, out.data(), &outlen,
                         plaintext.data(), plaintext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx); return {};
    }
    total = outlen;

    if (EVP_EncryptFinal_ex(ctx, out.data() + outlen, &outlen) != 1) {
        EVP_CIPHER_CTX_free(ctx); return {};
    }
    total += outlen;
    out.resize(total);

    EVP_CIPHER_CTX_free(ctx);
    return out;
}

// ================================================================
// AES-128-CBC + PKCS7 解密
// ================================================================
std::vector<uint8_t> MiioClient::aesDecrypt(const std::vector<uint8_t>& ciphertext) const
{
    uint8_t key[16], iv[16];
    deriveKeyIv(key, iv);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    std::vector<uint8_t> out(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);
    int outlen = 0, total = 0;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr, key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx); return {};
    }
    EVP_CIPHER_CTX_set_padding(ctx, 1);

    if (EVP_DecryptUpdate(ctx, out.data(), &outlen,
                         ciphertext.data(), ciphertext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx); return {};
    }
    total = outlen;

    if (EVP_DecryptFinal_ex(ctx, out.data() + outlen, &outlen) != 1) {
        EVP_CIPHER_CTX_free(ctx); return {};
    }
    total += outlen;
    out.resize(total);

    EVP_CIPHER_CTX_free(ctx);
    return out;
}

// ================================================================
// MD5
// ================================================================
std::vector<uint8_t> MiioClient::md5(const uint8_t* data, size_t len)
{
    std::vector<uint8_t> digest(MD5_DIGEST_LENGTH);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
    EVP_DigestUpdate(ctx, data, len);
    unsigned int outlen = 0;
    EVP_DigestFinal_ex(ctx, digest.data(), &outlen);
    EVP_MD_CTX_free(ctx);
    return digest;
}

// ================================================================
// hex → bytes
// ================================================================
std::vector<uint8_t> MiioClient::hexToBytes(const std::string& hex)
{
    std::vector<uint8_t> out;
    if (hex.size() % 2 != 0) return out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        char buf[3] = {hex[i], hex[i+1], 0};
        char* end = nullptr;
        long v = strtol(buf, &end, 16);
        if (*end != 0) return {};
        out.push_back((uint8_t)v);
    }
    return out;
}

} // namespace miio

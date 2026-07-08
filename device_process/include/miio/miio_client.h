#ifndef MIIO_CLIENT_H
#define MIIO_CLIENT_H

#include <string>
#include <vector>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <nlohmann/json.hpp>

// ================================================================
// miio 协议客户端（C++ 自实现）
// ----------------------------------------------------------------
// 严格按照 python-miio 的 miio/protocol.py 实现（权威参考）：
// https://github.com/rytilahti/python-miio/blob/master/miio/protocol.py
//
// 包结构（32 字节固定头 + 可选加密 payload）：
//
//   偏移  长度  字段          说明
//   0     2     magic        0x2131（大端）
//   2     2     length       总长度（含头32B，大端）
//   4     4     unknown      固定 0x00000000
//   8     4     device_id    设备 ID（hello 填 0，RPC 填握手得到的 did）
//   12    4     ts           设备 uptime（秒，从 hello 响应获取，非 Unix 时间戳！）
//   16    16    checksum     MD5 校验（hello 全0；RPC = md5(header+token+ciphertext)）
//   32    N     payload      AES-128-CBC 加密的 JSON
//
// 加密（关键！不是 ECB）：
//   key = md5(token)
//   iv  = md5(key + token) = md5(md5(token) + token)
//   算法：AES-128-CBC + PKCS7
//   明文：JSON 序列化后追加 \x00 字节
//
// checksum（RPC 包）：
//   md5(header_16B + token_16B + ciphertext)
//   注意顺序：header + token + data（不是 header + data + token）
// ================================================================

namespace miio {

class MiioClient {
public:
    MiioClient(const std::string& ip, uint16_t port, const std::string& token_hex);
    ~MiioClient();

    MiioClient(const MiioClient&) = delete;
    MiioClient& operator=(const MiioClient&) = delete;

    bool connect();
    bool isConnected() const { return connected_; }

    bool rpcCall(const std::string& method,
                 const nlohmann::json& params,
                 nlohmann::json& response,
                 int timeout_ms = 3000);

    bool getProperties(const std::vector<std::pair<int,int>>& props,
                      nlohmann::json& values,
                      int timeout_ms = 3000);

    bool setProperties(const nlohmann::json& props,
                      int timeout_ms = 3000);

    bool callAction(int siid, int aiid,
                    const nlohmann::json& in_params,
                    nlohmann::json& out_params,
                    int timeout_ms = 3000);

    uint32_t getDid() const { return did_; }

private:
    bool sendAndReceive(const std::vector<uint8_t>& send_buf,
                       std::vector<uint8_t>& recv_buf,
                       int timeout_ms);

    std::vector<uint8_t> buildRpcPacket(const std::string& json_payload);
    bool parseResponse(const std::vector<uint8_t>& buf, std::string& json_out);

    // AES-128-CBC + PKCS7，key/iv 由 token 派生
    std::vector<uint8_t> aesEncrypt(const std::vector<uint8_t>& plaintext) const;
    std::vector<uint8_t> aesDecrypt(const std::vector<uint8_t>& ciphertext) const;

    // MD5 摘要
    static std::vector<uint8_t> md5(const uint8_t* data, size_t len);

    // key/iv 派生
    void deriveKeyIv(uint8_t key[16], uint8_t iv[16]) const;

    static std::vector<uint8_t> hexToBytes(const std::string& hex);

    std::string ip_;
    uint16_t   port_;
    uint8_t    token_[16];
    int        sock_fd_;
    mutable std::mutex mutex_;

    bool       connected_;
    uint32_t   did_;
    uint32_t   device_ts_;   // 设备 uptime（秒），从 hello 响应获取，每次 RPC 递增
    uint64_t   msg_id_;
};

} // namespace miio

#endif // MIIO_CLIENT_H

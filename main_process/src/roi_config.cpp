#include "roi_config.h"
#include <stdio.h>
#include <string.h>

extern AppConfig g_cfg; 

int roi_config_load(const char* path, ROIConfig* cfg)
{
    int actual_streams = g_cfg.channel_count;
    FILE* f = fopen(path, "r");
    if (!f) {   //文件不存在，后面改处理
        printf("[ROI] 配置文件 %s 不存在，使用默认（全屏检测）\n", path);
        cfg->stream_count = actual_streams; 
        cfg->person_stay_threshold = 3000;
        cfg->absence_threshold = 3000;
        for (int i = 0; i < actual_streams; i++) {
            cfg->streams[i].stream_id = i;
            cfg->streams[i].device_count = 1;
            cfg->streams[i].devices[0] = {0, 0, 640, 480, 0};  // 默认全屏
        }
        return 0;
    }

    // 走到这里，说明文件打开了，开始解析
    memset(cfg, 0, sizeof(ROIConfig));  // 先把整个结构体清零防止随机值
    cfg->person_stay_threshold = 3000;
    cfg->absence_threshold = 3000;

    char line[256];                 // 缓冲区：存一行的文本
    int stream_counters[MAX_CHANNEL] = {0};   

    //从文件指针f中读一行文本，存到line数组里最多读sizeof(line)-1个字符
    // 读到换行符\n为止，换行符也会被存进去
    // 文件读完返回NULL，循环结束
    while (fgets(line, sizeof(line), f)) {
        //流编号，设备号，roi左上角坐标，roi宽高
        int sid, did, x, y, w, h;   
        //从line解析出6个整数给对应变量，成功解析六个才有效
        if (sscanf(line, "%d %d %d %d %d %d", &sid, &did, &x, &y, &w, &h) == 6) {
            if (sid >= actual_streams) continue; // 流号超出范围，跳过
            int idx = stream_counters[sid]++;   //先取值再自增，代表这个sid的第n个设备
            cfg->streams[sid].stream_id = sid;  //流号存储
            cfg->streams[sid].device_count = idx + 1;//真实设备id从1开始
            cfg->streams[sid].devices[idx] = {x, y, w, h, did};//ROI区域
        }
    }
    fclose(f);

    //防止只配置了单独几路有的路没配置，就用默认的
    cfg->stream_count = actual_streams;
    for (int i = 0; i < actual_streams; i++) {
        if (cfg->streams[i].device_count == 0) {// ← 只有device_count=0才进去
            cfg->streams[i].stream_id = i;
            cfg->streams[i].device_count = 1;
            cfg->streams[i].devices[0] = {0, 0, 640, 480, 0};
        }
    }

    printf("[ROI] 加载完成: %d 路", cfg->stream_count);
    for (int i = 0; i < cfg->stream_count; i++)
        printf(" 流%d:%d设备", i, cfg->streams[i].device_count);
    printf("\n");
    return 0;
}

// 判断检测框是否在设备区域内
// 判断标准：检测框的中心点是否落入ROI矩形
int roi_is_inside(const DeviceROI* roi, int box_x, int box_y, int box_w, int box_h)
{
    // 检测框中心点是否在 ROI 内
    int cx = box_x + box_w / 2;
    int cy = box_y + box_h / 2;

    return (cx >= roi->x && cx <= roi->x + roi->w &&
            cy >= roi->y && cy <= roi->y + roi->h);
}
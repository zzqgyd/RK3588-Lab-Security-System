#pragma once
#define MAX_DETECTIONS 64

#ifndef IPC_DETECTION_BOX_DEFINED
#define IPC_DETECTION_BOX_DEFINED
struct DetectionBox {
    int x, y, w, h;
};
#endif
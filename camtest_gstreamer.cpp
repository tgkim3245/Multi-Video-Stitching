#include <opencv2/opencv.hpp>
#include <iostream>
#include <thread>
#include <chrono>

using namespace std;
using namespace cv;

int main() {
    cout << "OpenCV version: " << CV_VERSION << endl;
    
    // GStreamer 파이프라인을 사용한 카메라 열기
    // libcamera -> GStreamer -> OpenCV
    string gstreamer_pipeline = 
        "libcamerasrc ! "
        "video/x-raw,width=640,height=480,format=NV12 ! "
        "videoconvert ! "
        "video/x-raw,format=BGR ! "
        "appsink";
    
    cout << "Opening camera with GStreamer pipeline..." << endl;
    cout << "Pipeline: " << gstreamer_pipeline << endl << endl;
    
    VideoCapture camera(gstreamer_pipeline, CAP_GSTREAMER);
    
    // 카메라 열림 확인
    if (!camera.isOpened()) {
        cerr << "Failed to open camera with GStreamer!" << endl;
        return -1;
    }
    
    cout << "✓ Camera opened successfully!" << endl;
    cout << "Resolution: " << camera.get(CAP_PROP_FRAME_WIDTH) 
         << "x" << camera.get(CAP_PROP_FRAME_HEIGHT) << endl;
    cout << "\nPress 'q' or ESC to exit." << endl << endl;
    
    Mat frame;
    int frame_count = 0;
    
    while (true) {
        // 프레임 캡처
        if (!camera.read(frame)) {
            cerr << "Failed to capture frame!" << endl;
            this_thread::sleep_for(chrono::milliseconds(100));
            continue;
        }
        
        if (frame.empty()) {
            continue;
        }
        
        frame_count++;
        
        // 텍스트 추가 (프레임 수 표시)
        string text = "Frame: " + to_string(frame_count);
        putText(frame, text, Point(10, 30), FONT_HERSHEY_SIMPLEX, 1, Scalar(0, 255, 0), 2);
        
        // 화면에 표시
        imshow("Camera Feed (GStreamer + libcamera)", frame);
        
        if (frame_count % 30 == 0) {
            cout << "Captured " << frame_count << " frames" << endl;
        }
        
        // q 또는 ESC로 종료
        int key = waitKey(30);
        if (key == 'q' || key == 27) {
            cout << "\nExit... (captured " << frame_count << " frames)" << endl;
            break;
        }
    }
    
    // 정리
    camera.release();
    destroyAllWindows();
    
    return 0;
}

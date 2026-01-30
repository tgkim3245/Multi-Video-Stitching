#include <opencv2/opencv.hpp>
#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>

using namespace std;
using namespace cv;

int main() {
    cout << "[SENDER] Starting..." << endl;
    cout.flush();
    
    // GStreamer 파이프라인 (libcamera + GStreamer)
    string gstreamer_pipeline = 
        "libcamerasrc ! "
        "video/x-raw,width=640,height=480,format=NV12 ! "
        "videoconvert ! "
        "video/x-raw,format=BGR ! "
        "appsink";
    
    cout << "[SENDER] Opening camera with GStreamer + libcamera..." << endl;
    cout.flush();
    
    VideoCapture camera(gstreamer_pipeline, CAP_GSTREAMER);
    
    cout << "[SENDER] Camera object created, checking if opened..." << endl;
    cout.flush();
    
    bool camera_opened = false;
    
    if (!camera.isOpened()) {
        cerr << "[SENDER] ERROR: Failed to open camera with GStreamer! Trying default camera..." << endl;
        cerr.flush();
        
        // Fallback: 기본 카메라 (USB 카메라, /dev/video0 등)
        cout << "[SENDER] Attempting to open default camera (index 0)..." << endl;
        camera.open(0);
    }
    
    if (camera.isOpened()) {
        camera_opened = true;
        cout << "[SENDER] ✓ Camera opened successfully!" << endl;
    } else {
        cerr << "[SENDER] WARNING: Could not open camera, but will continue to display loop!" << endl;
    }
    cout.flush();
    
    cout << "[SENDER] ✓ Camera opened successfully!" << endl;
    cout.flush();
    
    cout << "[SENDER] Attempting to display camera feed with imshow..." << endl;
    cout.flush();
    
    // Try to show camera feed immediately if opened
    Mat test_frame;
    if (camera_opened && camera.read(test_frame) && !test_frame.empty()) {
        imshow("Sender Camera", test_frame);
        cout << "[SENDER] ✓ Camera feed displayed! (" << test_frame.cols << "x" << test_frame.rows << ")" << endl;
        cout.flush();
        waitKey(1);
    } else if (!camera_opened) {
        cout << "[SENDER] Camera not opened yet, will try in main loop..." << endl;
        cout.flush();
    }
    
    cout << "[SENDER] ===== Socket Connection Phase (Optional) =====" << endl;
    cout.flush();
    
    // Receiver IP and port
    const char* RECEIVER_IP = "192.168.0.26";
    const int RECEIVER_PORT = 5000;
    
    cout << "[SENDER] Creating socket..." << endl;
    cout.flush();
    
    // Create TCP socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("[SENDER] Failed to create socket");
        cout << "[SENDER] WARNING: Socket creation failed, but will still display camera feed!" << endl;
        cout.flush();
        sockfd = -1;
    } else {
        cout << "[SENDER] Socket created, connecting to " << RECEIVER_IP << ":" << RECEIVER_PORT << endl;
        cout.flush();
        
        // Set receiver address
        struct sockaddr_in receiver_addr;
        memset(&receiver_addr, 0, sizeof(receiver_addr));
        receiver_addr.sin_family = AF_INET;
        receiver_addr.sin_port = htons(RECEIVER_PORT);
        
        if (inet_pton(AF_INET, RECEIVER_IP, &receiver_addr.sin_addr) <= 0) {
            cerr << "[SENDER] Invalid IP address" << endl;
            close(sockfd);
            sockfd = -1;
        } else {
            // Connect to receiver
            cout << "[SENDER] Connecting to receiver..." << endl;
            cout.flush();
            
            if (connect(sockfd, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr)) < 0) {
                perror("[SENDER] Connection failed");
                cout << "[SENDER] WARNING: Connection failed to " << RECEIVER_IP << ":" << RECEIVER_PORT << endl;
                cout << "[SENDER] Check if receiver is running at that address!" << endl;
                cout << "[SENDER] But will still display camera feed!" << endl;
                cout.flush();
                close(sockfd);
                sockfd = -1;
            } else {
                cout << "[SENDER] ✓ Connection successful to " << RECEIVER_IP << ":" << RECEIVER_PORT << "!" << endl;
                cout << "[SENDER] Starting video transmission..." << endl;
                cout.flush();
            }
        }
    }
    
    cout << "[SENDER] ===== Starting camera display loop =====" << endl;
    cout.flush();
    
    Mat frame;
    int frame_count = 0;
    bool first_frame = true;
    
    while (true) {
        // Capture frame
        if (!camera_opened || !camera.read(frame)) {
            if (first_frame && camera_opened) {
                cerr << "Failed to capture frame" << endl;
            }
            continue;
        }
        
        if (frame.empty()) {
            continue;
        }
        
        if (first_frame) {
            cout << "[SENDER] ✓ First frame captured! (" << frame.cols << "x" << frame.rows << ")" << endl;
            cout.flush();
            first_frame = false;
        }
        
        // Display frame - 통신연결 여부와 관계없이 항상 imshow 실행
        imshow("Sender Camera", frame);
        
        // Encode frame to JPEG
        vector<uchar> buffer;
        vector<int> compression_params;
        compression_params.push_back(IMWRITE_JPEG_QUALITY);
        compression_params.push_back(80);  // Quality (0-100)
        
        imencode(".jpg", frame, buffer, compression_params);
        
        // Only send if connected
        if (sockfd >= 0) {
            // Send frame size (4 bytes, big-endian)
            uint32_t frame_size = buffer.size();
            unsigned char size_buffer[4];
            size_buffer[0] = (frame_size >> 24) & 0xFF;
            size_buffer[1] = (frame_size >> 16) & 0xFF;
            size_buffer[2] = (frame_size >> 8) & 0xFF;
            size_buffer[3] = frame_size & 0xFF;
            
            if (send(sockfd, size_buffer, 4, 0) < 0) {
                cout << "[SENDER] ERROR: Failed to send frame size! " << strerror(errno) << endl;
                cout.flush();
                close(sockfd);
                sockfd = -1;
            } else if (send(sockfd, buffer.data(), buffer.size(), 0) < 0) {
                cout << "[SENDER] ERROR: Failed to send frame data! " << strerror(errno) << endl;
                cout.flush();
                close(sockfd);
                sockfd = -1;
            } else {
                frame_count++;
                if (frame_count == 1) {
                    cout << "[SENDER] ✓ First frame transmitted! (" << buffer.size() << " bytes)" << endl;
                    cout.flush();
                } else if (frame_count % 30 == 0) {
                    cout << "[SENDER] Frame " << frame_count << " transmitted: " << buffer.size() << " bytes" << endl;
                    cout.flush();
                }
            }
        } else {
            if (frame_count == 0) {
                cout << "[SENDER] WARNING: Socket not connected (sockfd=" << sockfd << "), not sending frames" << endl;
                cout.flush();
            }
        }
        
        // q or ESC to exit
        int key = waitKey(30);
        if (key == 'q' || key == 27) {
            cout << "[SENDER] Exit..." << endl;
            break;
        }
    }
    
    // Cleanup
    close(sockfd);
    camera.release();
    destroyAllWindows();
    
    return 0;
}


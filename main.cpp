#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <windows.h>
#define close closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstring>
#include <iostream>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <thread>

using namespace std;
using namespace cv;

Mat received_frame;
Mat local_frame;
mutex frame_mutex;
bool connection_active = true;

void receiveFrames(int client_socket) {
  unsigned char size_buffer[4];
  int frame_count = 0;
  bool first_frame = true;

  cout << "[RECV] Starting to receive frames..." << endl;

  while (connection_active) {
    // Receive frame size (4 bytes)
    int recv_size = recv(client_socket, (char *)size_buffer, 4, 0);
    if (recv_size <= 0) {
      cout << "[RECV] Connection closed (received " << frame_count << " frames)"
           << endl;
      break;
    }

    // Convert from big-endian
    uint32_t frame_size = (size_buffer[0] << 24) | (size_buffer[1] << 16) |
                          (size_buffer[2] << 8) | size_buffer[3];

    if (first_frame) {
      cout << "[RECV] First frame size header received: " << frame_size
           << " bytes" << endl;
      first_frame = false;
    }

    // Frame data buffer
    vector<uchar> frame_buffer(frame_size);

    // Receive frame data
    uint32_t received = 0;
    while (received < frame_size) {
      int bytes = recv(client_socket, (char *)(frame_buffer.data() + received),
                       frame_size - received, 0);
      if (bytes <= 0) {
        cout << "[ERROR] Failed to receive frame data at " << received << "/"
             << frame_size << endl;
        break;
      }
      received += bytes;
    }

    // JPEG decode
    Mat frame = imdecode(frame_buffer, IMREAD_COLOR);

    if (!frame.empty()) {
      {
        lock_guard<mutex> lock(frame_mutex);
        received_frame = frame.clone();
      }
      frame_count++;
      if (frame_count == 1) {
        cout << "[RECV] ✓ First frame decoded successfully! (" << frame.cols
             << "x" << frame.rows << ")" << endl;
      }
      if (frame_count % 30 == 0) {
        cout << "[RECV] Frame " << frame_count << " received: " << frame_size
             << " bytes (" << frame.cols << "x" << frame.rows << ")" << endl;
      }
    } else {
      cout << "[ERROR] Failed to decode frame (size: " << frame_size
           << " bytes)" << endl;
    }
  }

  close(client_socket);
  cout << "[RECV] Receive thread finished. Total frames received: "
       << frame_count << endl;
  connection_active = false;
}

void captureLocalCamera() {
  // GStreamer 파이프라인 (libcamera + GStreamer)
  string gstreamer_pipeline = "libcamerasrc ! "
                              "video/x-raw,width=640,height=480,format=NV12 ! "
                              "videoconvert ! "
                              "video/x-raw,format=BGR ! "
                              "appsink";

  VideoCapture camera(gstreamer_pipeline, CAP_GSTREAMER);

  if (!camera.isOpened()) {
    cerr << "Failed to open local camera with GStreamer!" << endl;
    connection_active = false;
    return;
  }

  cout << "Local camera capture started (GStreamer + libcamera)" << endl;

  Mat frame;
  while (connection_active) {
    if (!camera.read(frame)) {
      continue;
    }

    if (!frame.empty()) {
      {
        lock_guard<mutex> lock(frame_mutex);
        local_frame = frame.clone();
      }
    }
  }

  camera.release();
}

int main() {
#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    std::cerr << "WSAStartup failed.\n";
    return -1;
  }
#endif
  const int LISTEN_PORT = 5000;

  // Create TCP socket
  int server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket < 0) {
    perror("Failed to create socket");
    return -1;
  }

  // Set SO_REUSEADDR option (port reuse)
  int opt = 1;
  if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
      0) {
    perror("Failed to set socket option");
    close(server_socket);
    return -1;
  }

  // Set server address
  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(LISTEN_PORT);

  // Bind
  if (bind(server_socket, (struct sockaddr *)&server_addr,
           sizeof(server_addr)) < 0) {
    perror("Bind failed");
    close(server_socket);
    return -1;
  }

  // Start listening
  if (listen(server_socket, 1) < 0) {
    perror("Listen failed");
    close(server_socket);
    return -1;
  }

  cout << "Waiting for connections on port " << LISTEN_PORT << "..." << endl;

  // Start local camera capture thread immediately
  thread camera_thread(captureLocalCamera);

  // Set non-blocking mode for accept
#ifdef _WIN32
  u_long mode = 1;
  if (ioctlsocket(server_socket, FIONBIO, &mode) != 0) {
    std::cerr << "ioctlsocket failed\n";
  }
#else
  int flags = fcntl(server_socket, F_GETFL, 0);
  fcntl(server_socket, F_SETFL, flags | O_NONBLOCK);
#endif

  int client_socket = -1;
  thread *receive_thread = nullptr; // Use pointer to manage thread lifecycle

  // 클라이언트 연결 시도 (논-블로킹)
  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);

  cout << "Waiting for connections on port " << LISTEN_PORT
       << " (local camera running)..." << endl;

  cout << "\nStitching started (Press ESC or 'q' to exit)\n" << endl;

  // Main thread stitching and display
  while (connection_active) {
    // Try to accept client connection (non-blocking)
    if (client_socket < 0) {
      int new_socket = accept(server_socket, (struct sockaddr *)&client_addr,
                              &client_addr_len);

      if (new_socket >= 0) {
        client_socket = new_socket;
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        cout << "Client connected: " << client_ip << ":"
             << ntohs(client_addr.sin_port) << endl;

        // Clean up old thread if it exists
        if (receive_thread != nullptr && receive_thread->joinable()) {
          receive_thread->join();
          delete receive_thread;
        }

        // Start receiving thread
        receive_thread = new thread(receiveFrames, client_socket);
      }
    }
    Mat stitch_result;

    {
      lock_guard<mutex> lock(frame_mutex);

      if (!local_frame.empty() && !received_frame.empty()) {
        // Received video (left)
        std::vector<cv::Mat> imgs;
        imgs.push_back(received_frame);
        imgs.push_back(local_frame);

        // 영상 스티칭을 수행할 객체 생성 (루프 밖에서 생성하는 것이 좋으나,
        // 모드 변경이나 재설정이 필요할 수 있어 간단한 예시로 여기에 둠.
        // 최적화를 위해 static 또는 main 함수 초반에 선언 추천)
        // SCANS 모드는 빠르지만 특징점이 적으면 크래시가 발생할 수 있어
        // PANORAMA로 변경
        static auto stitcher = cv::Stitcher::create(cv::Stitcher::PANORAMA);

        cv::Mat pano;
        cv::Stitcher::Status status = cv::Stitcher::ERR_NEED_MORE_IMGS;

        try {
          status = stitcher->stitch(imgs, pano);
        } catch (const cv::Exception &e) {
          std::cerr << "Stitching crash prevented: " << e.what() << std::endl;
          status =
              cv::Stitcher::ERR_CAMERA_PARAMS_ADJUST_FAIL; // Fallback triggers
        }

        if (status == cv::Stitcher::OK) {
          cv::putText(pano, "Stitched Panorama", cv::Point(20, 30),
                      cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);
          cv::imshow("Dual RPi Streams", pano);
        } else {
          // 스티칭 실패 시 원본 영상 나란히 표시 (Fallback)
          // 크기가 다를 경우를 대비해 resize (필요 시)
          cv::Mat frame1_resized = received_frame.clone();
          cv::Mat frame2_resized = local_frame.clone();

          if (frame1_resized.rows != frame2_resized.rows) {
            // Resize the smaller frame to match the height of the larger one
            if (frame1_resized.rows < frame2_resized.rows) {
              cv::resize(frame1_resized, frame1_resized,
                         cv::Size(frame2_resized.cols * frame1_resized.cols /
                                      frame1_resized.rows,
                                  frame2_resized.rows));
            } else {
              cv::resize(frame2_resized, frame2_resized,
                         cv::Size(frame1_resized.cols * frame2_resized.cols /
                                      frame2_resized.rows,
                                  frame1_resized.rows));
            }
          }
          cv::Mat combined;
          cv::hconcat(frame1_resized, frame2_resized, combined);

          std::string alert_msg =
              "Stitching Failed: " + std::to_string(int(status));
          cv::putText(combined, alert_msg, cv::Point(20, 30),
                      cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 0, 255), 2);
          cv::imshow("Dual RPi Streams", combined);
        }
      } else if (!local_frame.empty()) {
        imshow("Dual RPi Streams", local_frame);
      } else if (!received_frame.empty()) {
        imshow("Dual RPi Streams", received_frame);
      }
    }

    // Display original frames
    if (!local_frame.empty()) {
      imshow("Local Camera", local_frame);
    }
    if (!received_frame.empty()) {
      imshow("Received Video", received_frame);
    } else {
      // 수신 프레임이 없을 때 검은색 창 표시
      Mat black_frame = Mat::zeros(480, 640, CV_8UC3);
      imshow("Received Video", black_frame);
    }

    // Display stitching result
    // The stitching result is now displayed directly within the if-else block
    // above if (!stitch_result.empty()) {
    //   imshow("Stitched Result (Local + Received)", stitch_result);
    // }

    int key = waitKey(30);
    if (key == 27 || key == 'q') { // ESC or 'q' to exit
      cout << "\nExit signal detected..." << endl;
      break;
    }
  }

  // Wait for threads to finish
  connection_active = false;

  // Wait for camera thread
  camera_thread.join();

  // Properly clean up receive thread
  if (receive_thread != nullptr && receive_thread->joinable()) {
    receive_thread->join();
    delete receive_thread;
  }

  // Close client socket if connected
  if (client_socket >= 0) {
    close(client_socket);
  }

  // Cleanup
  close(server_socket);
  destroyAllWindows();

#ifdef _WIN32
  WSACleanup();
#endif

  return 0;
}

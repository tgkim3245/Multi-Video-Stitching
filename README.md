# opencv install
sudo apt install libopencv-dev

# build
g++ main.cpp -o receiver $(pkg-config --cflags --libs opencv4) -pthread
g++ sender.cpp -o sender $(pkg-config --cflags --libs opencv4) -pthread

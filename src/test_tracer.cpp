#include "tracer.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <map>
#include <vector>

int main()
{
#ifdef _WIN32
    Tracer tracer("COM10", 9600);
#else
    Tracer tracer("/dev/ttyUSB0", B9600);
#endif

    std::vector<uint8_t> header;
    header.push_back(0x00);
    header.push_back(0x00);

    bool success = tracer.start();
    if (!success)
    {
        return 0;
    }
    tracer.writeMessage(header);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    tracer.stop();
    std::vector<std::vector<uint8_t>> messages = tracer.getMessages();

    std::cout << "Messages received: " << messages.size() << "\n";

    std::map<size_t, size_t> sizeCounts;
    size_t totalBytes = 0;

    for (const auto &msg : messages)
    {
        sizeCounts[msg.size()]++;
        totalBytes += msg.size();
    }

    for (const auto &[size, count] : sizeCounts)
        std::cout << size << " bytes x" << count << ", ";

    std::cout << "\nTotal bytes received: " << totalBytes << std::endl;

    return 0;
}

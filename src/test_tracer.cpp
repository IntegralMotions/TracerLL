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

    tracer.start();

    std::vector<std::vector<uint8_t>> messages;
    for (int i = 0; i < 10; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto slice = tracer.getMessages();
        messages.insert(messages.end(),
                        std::make_move_iterator(slice.begin()),
                        std::make_move_iterator(slice.end()));
    }

    tracer.stop();

    std::cout << "Messages received: " << messages.size() << "\n";

    std::map<size_t, size_t> sizeCounts;
    for (const auto &msg : messages)
        sizeCounts[msg.size()]++;

    for (const auto &[size, count] : sizeCounts)
        std::cout << size << " bytes x" << count << ", ";

    std::cout << std::endl;
    return 0;
}

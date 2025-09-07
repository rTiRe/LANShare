#include "NetworkManager.h"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    NetworkManager nm(50000);
    nm.start();

    // Периодически печатаем список известных узлов
    for (int i = 0; i < 30; i++) {
        auto nodes = nm.getKnownNodes();
        std::cout << "Известные узлы: ";
        for (auto& n : nodes) {
            std::cout << n << " ";
        }
        std::cout << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    nm.stop();
    return 0;
}

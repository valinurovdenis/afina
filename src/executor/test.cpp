#include <iostream>
#include <thread>
#include "../../include/afina/Executor.h"

int main()
{
    std::cout << "Going to create\n";
    Afina::Executor ex("pool", 10, 20, 60, 1);
    std::cout << "Created executor\n";

    for (int i = 0; i < 50; i++) {
        //    std::cout << "Submitting job #" << i << std::endl;
        ex.Execute([i](){
            std::cout << "Hello #" << i << " from thread " << std::this_thread::get_id() << std::endl;});
    }

    std::cout << "Starting to stop executor...\n";
    ex.Stop(true);

    return 0;
}

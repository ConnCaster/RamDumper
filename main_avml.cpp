#include <iostream>
#include <bits/this_thread_sleep.h>

int main()
{
    const char* str = "[UID] hELLo_wORLd";
    char c = str[0];
    int i = 0;
    while (i++ < 100)
    {
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
    return 0;
}

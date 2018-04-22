#include <afina/coroutine/Engine.h>

#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <cstdlib>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
    char this_address;

    ctx.Low = &this_address;
    ctx.Hight = StackBottom;

    uint32_t size = uint32_t(abs(&this_address - StackBottom));
    if ((std::get<0>(ctx.Stack) == nullptr) || (size > std::get<1>(ctx.Stack))) {
        free(std::get<0>(ctx.Stack));
        std::get<0>(ctx.Stack) = (char *)malloc(sizeof(char) * size);
        std::get<1>(ctx.Stack) = size;
    }

    memcpy(std::get<0>(ctx.Stack), &this_address, std::get<1>(ctx.Stack));
}

void Engine::Restore(context &ctx) {
    char this_address;

    if (abs(&this_address - StackBottom) < std::get<1>(ctx.Stack)) {
        Restore(ctx);
    }
    memcpy(ctx.Low, std::get<0>(ctx.Stack), std::get<1>(ctx.Stack));
    longjmp(ctx.Environment, 1);
}

void Engine::yield() {
    context *next_routine = alive;
    while (next_routine == cur_routine && next_routine != nullptr){
        next_routine = next_routine->next;
    }

    if(next_routine != nullptr) {
        sched(next_routine);
    }
}

void Engine::sched(void *routine_) {
    context *called = reinterpret_cast<context *>(routine_);

    if (called && called != cur_routine) {
        if (cur_routine) {
            Store(*cur_routine);
            if (setjmp(cur_routine->Environment) > 0) {
                return;
            }
        }
        cur_routine = called;
        Restore(*called);
    } else {
        yield();
    }
}

} // namespace Coroutine
} // namespace Afina

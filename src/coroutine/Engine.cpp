#include <afina/coroutine/Engine.h>

#include <setjmp.h>
#include <stdexcept>
#include <stdio.h>
#include <string.h>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
    volatile char cur_stack;
    ctx.Hight = StackBottom;
    ctx.Low = (char *)&cur_stack;
    int size = ctx.Hight - ctx.Low;
    std::get<1>(ctx.Stack) = size;
    delete[] std::get<0>(ctx.Stack);
    std::get<0>(ctx.Stack) = new char[size];
    memcpy(std::get<0>(ctx.Stack), ctx.Low, size);
}

void Engine::Restore(context &ctx) {
    volatile char cur_stack = 0;
    if ((char *)&cur_stack > StackBottom - std::get<1>(ctx.Stack)) {
        char a[100];
        Restore(ctx);
    }
    memcpy(ctx.Low, std::get<0>(ctx.Stack), std::get<1>(ctx.Stack));
    longjmp(ctx.Environment, 1);
}

void Engine::yield() {
    if (this->StackBottom == 0) {
        return;
    }
    if (cur_routine == nullptr) {
        throw std::runtime_error("Engine::yield error");
    }
    // if alive is nullptr then all corutines are done and we have to return
    // to the Engine::Start
    // if there is no corutines except caller => return to caller
    if (alive == nullptr) {
        return;
    }
    context *routine;
    if (alive == cur_routine) {
        if (alive->next != nullptr) {
            routine = alive->next;
        } else {
            return;
        }
    } else {
        routine = alive;
    }
    // new routine is chosen, now start it
    if (setjmp(cur_routine->Environment) > 0) {
        return;
    } else {
        Store(*cur_routine);
        cur_routine = static_cast<context *>(routine);
        Restore(*cur_routine);
    }
}

void Engine::sched(void *routine_) {
    if (this->StackBottom == 0) {
        return;
    }
    if (cur_routine == nullptr) {
        throw std::runtime_error("Engine::sched error");
    }
    // if new routine not specified => return to caller
    if (routine_ == nullptr) {
        return;
    }
    if (setjmp(cur_routine->Environment) > 0) {
        return;
    } else {
        Store(*cur_routine);
        cur_routine = static_cast<context *>(routine_);
        Restore(*cur_routine);
    }
}

} // namespace Coroutine
} // namespace Afina

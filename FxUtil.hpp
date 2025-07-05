#pragma once

#include <utility>

/** Creates a new context that will call the given function at the end of scope */
template <typename FuncType>
class FxDeferObject
{
public:
    FxDeferObject(FuncType&& func) noexcept
        : mFunc(std::move(func))
    {
    }

    FxDeferObject(const FxDeferObject& other) = delete;
    FxDeferObject& operator = (const FxDeferObject& other) = delete;

    ~FxDeferObject() noexcept
    {
        mFunc();
    }

private:
    FuncType mFunc;
};

#define FX_CONCAT_INNER(a_, b_) a_##b_
#define FX_CONCAT(a_, b_) FX_CONCAT_INNER(a_, b_)

#define FxDefer(fn_) FxDeferObject FX_CONCAT(_ds_, __LINE__)(fn_)

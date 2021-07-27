#ifndef __NBL_I_INPUT_EVENT_CHANNEL_H_INCLUDED__
#define __NBL_I_INPUT_EVENT_CHANNEL_H_INCLUDED__

#include <mutex>
#include <chrono>
#include "nbl/core/IReferenceCounted.h"
#include "nbl/core/containers/CCircularBuffer.h"
#include "nbl/core/SRange.h"
#include "nbl/ui/KeyCodes.h"

namespace nbl {
namespace ui
{
class IWindow;
class IInputEventChannel : public core::IReferenceCounted
{
protected:
    virtual ~IInputEventChannel() = default;

public:
    // TODO Any other common (not depending on event struct type) member functions?
    enum E_TYPE
    {
        ET_MOUSE,
        ET_KEYBOARD,
        ET_MULTITOUCH,
        ET_GAMEPAD,

        ET_COUNT
    };

    virtual bool empty() const = 0;
    virtual E_TYPE getType() const = 0;
};

namespace impl
{
    template <typename EventType>
    class IEventChannelBase : public IInputEventChannel
    {
    protected:
        using cb_t = core::CConstantRuntimeSizedCircularBuffer<EventType>;
        using iterator_t = typename cb_t::iterator;
        using range_t = core::SRange<EventType, iterator_t, iterator_t>;

        std::mutex m_bgEventBufMtx;
        cb_t m_bgEventBuf;
        cb_t m_frontEventBuf;
    public:
        // Lock while working with background event buffer
        std::unique_lock<std::mutex> lockBackgroundBuffer()
        {
            return std::unique_lock<std::mutex>(m_bgEventBufMtx);
        }

        // Use this within OS-specific impl (Windows callback/XNextEvent loop thread/etc...)
        void pushIntoBackground(EventType&& ev)
        {
            m_bgEventBuf.push_back(std::move(ev));
        }

        // WARNING: Access to getEvents() must be externally synchronized to be safe!
        // (the function returns range of iterators)
        range_t getEvents()
        {
            downloadFromBackgroundIntoFront();
            return range_t(m_frontEventBuf.begin(), m_frontEventBuf.end());
        }

        virtual ~IEventChannelBase() = default;
        explicit IEventChannelBase(size_t _circular_buffer_capacity) : 
            m_bgEventBuf(_circular_buffer_capacity),
            m_frontEventBuf(_circular_buffer_capacity)
        {

        }

    private:
        void downloadFromBackgroundIntoFront()
        {
            auto lk = lockBackgroundBuffer();

            while (m_bgEventBuf.size() > 0ull)
            {
                auto& ev = m_bgEventBuf.pop_front();
                m_frontEventBuf.push_back(std::move(ev));
            }
        }

    public:
        bool empty() const override final
        {
            return m_bgEventBuf.size() == 0ull;
        }
    };
}

struct SMouseEvent
{
    SMouseEvent()
    {
        using namespace std::chrono;
        timeStamp = duration_cast<microseconds>(system_clock::now().time_since_epoch());
    }
    enum E_EVENT_TYPE : uint8_t
    {
        EET_UNITIALIZED = 0,
        EET_CLICK = 1,
        EET_SCROLL = 2, 
        EET_MOVEMENT = 4
    } type = EET_UNITIALIZED;
    struct SClickEvent
    {
        int16_t clickPosX, clickPosY;
        ui::E_MOUSE_BUTTON mouseButton;
        enum E_ACTION : uint8_t
        {
            EA_UNITIALIZED = 0,
            EA_PRESSED = 1,
            EA_RELEASED = 2
        } action = EA_UNITIALIZED;
    };
    struct SScrollEvent
    {
        int16_t verticalScroll, horizontalScroll;
    };
    struct SMovementEvent
    {
        int16_t movementX, movementY;
    };
    union
    {
        SClickEvent clickEvent;
        SScrollEvent scrollEvent;
        SMovementEvent movementEvent;
    };
    IWindow* window;
    std::chrono::microseconds timeStamp;
};

class IMouseEventChannel : public impl::IEventChannelBase<SMouseEvent>
{
    using base_t = impl::IEventChannelBase<SMouseEvent>;

protected:
    virtual ~IMouseEventChannel() = default;

public:
    IMouseEventChannel(size_t circular_buffer_capacity) : base_t(circular_buffer_capacity)
    {

    }
    using base_t::base_t;

    E_TYPE getType() const override final
    { 
        return ET_MOUSE;
    }
};

struct SKeyboardEvent
{
    SKeyboardEvent() {
        using namespace std::chrono;
        timeStamp = duration_cast<microseconds>(system_clock::now().time_since_epoch());
    }
    enum E_KEY_ACTION : uint8_t
    {
        ECA_UNITIALIZED = 0,
        ECA_PRESSED = 1,
        ECA_RELEASED = 2
    } action = ECA_UNITIALIZED;
    ui::E_KEY_CODE keyCode = ui::EKC_NONE;
    IWindow* window = nullptr;
    std::chrono::microseconds timeStamp;
};

// TODO left/right shift/ctrl/alt kb flags
class IKeyboardEventChannel : public impl::IEventChannelBase<SKeyboardEvent>
{
    using base_t = impl::IEventChannelBase<SKeyboardEvent>;

protected:
    virtual ~IKeyboardEventChannel() = default;

public:
    using base_t::base_t;
    IKeyboardEventChannel(size_t circular_buffer_capacity) : base_t(circular_buffer_capacity)
    {

    }

    E_TYPE getType() const override final
    {
        return ET_KEYBOARD;
    }
};

}
}

#endif
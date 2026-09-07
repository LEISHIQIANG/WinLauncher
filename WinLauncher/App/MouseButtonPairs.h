#pragma once
#include <atomic>
#include <cstdint>

// Each bit belongs to one physical button. Policy changes cannot change the
// disposition of an up after its down was consumed. No wall-clock expiry.
class MouseButtonPairs
{
public:
    void Down(uint32_t button, bool consumed)
    {
        if (consumed) m_consumed.fetch_or(button);
        else m_consumed.fetch_and(~button);
    }
    bool Up(uint32_t button) { return (m_consumed.fetch_and(~button) & button) != 0; }
    uint32_t Snapshot() const { return m_consumed.load(); }
private:
    std::atomic<uint32_t> m_consumed{0};
};

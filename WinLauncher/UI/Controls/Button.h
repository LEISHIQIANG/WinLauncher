#pragma once
#include "IControl.h"
#include "../../Config/UIStyle.h"
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wrl.h>
#include <string>

using Microsoft::WRL::ComPtr;

class Button : public IControl
{
public:
    enum Style { Normal, Accent, Danger, Close };

    Button(std::wstring text = L"")
        : m_text(std::move(text))
    {
    }

    void SetText(const std::wstring& text) { m_text = text; }
    const std::wstring& GetText() const { return m_text; }

    void SetStyle(Style s) { m_style = s; }
    Style GetStyle() const { return m_style; }

    void SetFont(IDWriteTextFormat* font) { m_font = font; }

    bool IsHovered() const { return m_hovered; }
    bool IsPressed() const { return m_pressed; }

    // IControl
    virtual void OnPaint(ID2D1HwndRenderTarget* rt, float scale) override
    {
        if (!rt) return;

        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(m_bounds, m_radius, m_radius);
        ComPtr<ID2D1SolidColorBrush> bg;
        ComPtr<ID2D1SolidColorBrush> border;

        switch (m_style)
        {
        case Accent:
            rt->CreateSolidColorBrush(
                m_pressed ? UIStyle::ThemeColor::AccentHover().d2d :
                m_hovered ? UIStyle::ThemeColor::AccentHover().d2d :
                            UIStyle::ThemeColor::Accent().d2d, &bg);
            break;
        case Danger:
            rt->CreateSolidColorBrush(
                m_hovered ? D2D1::ColorF(0.82f, 0.18f, 0.18f, 0.72f) :
                            D2D1::ColorF(0.72f, 0.12f, 0.12f, 0.52f), &bg);
            break;
        case Close:
            if (m_hovered)
                rt->CreateSolidColorBrush(D2D1::ColorF(UIStyle::ThemeColor::DangerRed().d2d.r, UIStyle::ThemeColor::DangerRed().d2d.g, UIStyle::ThemeColor::DangerRed().d2d.b, 0.4f), &bg);
            break;
        default:
            rt->CreateSolidColorBrush(
                m_hovered ? UIStyle::ThemeColor::ButtonBgHover().d2d :
                            UIStyle::ThemeColor::ButtonBgNormal().d2d, &bg);
            break;
        }

        if (bg)
        {
            rt->FillRoundedRectangle(rr, bg.Get());
        }

        if (m_style != Close)
        {
            D2D1_COLOR_F borderClr = UIStyle::ThemeColor::ButtonBorderNormal().d2d;
            if (m_style == Accent)
            {
                borderClr = UIStyle::ThemeColor::AccentHover().d2d;
                borderClr.a = m_hovered || m_pressed ? 0.36f : 0.24f;
            }
            else if (m_style == Danger)
            {
                borderClr = D2D1::ColorF(0.95f, 0.30f, 0.30f, m_hovered ? 0.34f : 0.22f);
            }
            else if (m_hovered)
            {
                borderClr = UIStyle::ThemeColor::ButtonBorderHover().d2d;
            }
            rt->CreateSolidColorBrush(borderClr, &border);
            if (border)
            {
                rt->DrawRoundedRectangle(rr, border.Get(), UIStyle::Metrics::ControlStroke());
            }
        }

        if (m_style != Close && m_font && !m_text.empty())
        {
            ComPtr<ID2D1SolidColorBrush> tb;
            D2D1_COLOR_F textClr = (m_style == Accent || m_style == Danger) ?
                UIStyle::ThemeColor::TextOnAccent().d2d : UIStyle::ThemeColor::TextNormal().d2d;
            rt->CreateSolidColorBrush(textClr, &tb);
            if (tb)
            {
                rt->DrawTextW(m_text.c_str(), (UINT32)m_text.size(), m_font, m_bounds, tb.Get());
            }
        }
        else if (m_style == Close)
        {
            ComPtr<ID2D1SolidColorBrush> xb;
            rt->CreateSolidColorBrush(UIStyle::ThemeColor::TextMuted().d2d, &xb);
            if (xb)
            {
                float cx = (m_bounds.left + m_bounds.right) / 2.0f;
                float cy = (m_bounds.top + m_bounds.bottom) / 2.0f;
                rt->DrawLine(D2D1::Point2F(cx - 5, cy - 5), D2D1::Point2F(cx + 5, cy + 5), xb.Get(), UIStyle::Metrics::IconStroke());
                rt->DrawLine(D2D1::Point2F(cx + 5, cy - 5), D2D1::Point2F(cx - 5, cy + 5), xb.Get(), UIStyle::Metrics::IconStroke());
            }
        }
    }

    virtual bool OnMouseMove(POINT pt, float scale) override
    {
        bool now = HitTest(pt, scale);
        if (now != m_hovered)
        {
            m_hovered = now;
            return true;
        }
        return false;
    }

    virtual bool OnLButtonDown(POINT pt, float scale) override
    {
        if (HitTest(pt, scale))
        {
            m_pressed = true;
            return true;
        }
        return false;
    }

    virtual bool OnLButtonUp(POINT pt, float scale) override
    {
        bool wasPressed = m_pressed;
        m_pressed = false;
        if (wasPressed && HitTest(pt, scale))
        {
            if (m_onClick) m_onClick();
            return true;
        }
        return false;
    }

    virtual void OnResize(const D2D1_RECT_F& bounds) override
    {
        m_bounds = bounds;
    }

    void SetOnClick(std::function<void()> cb) { m_onClick = std::move(cb); }
    void SetRadius(float r) { m_radius = r; }

private:
    std::wstring m_text;
    Style m_style = Normal;
    IDWriteTextFormat* m_font = nullptr;
    bool m_hovered = false;
    bool m_pressed = false;
    float m_radius = 4.0f;
    std::function<void()> m_onClick;
};

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"
#include "DarkLookAndFeel.h"

namespace tl
{
    //==============================================================================
    /** Logo horizontal usado no topo dos plugins (alinhado à direita, altura fixa). */
    class LogoBadge : public juce::Component
    {
    public:
        LogoBadge() { setInterceptsMouseClicks (false, false); }

        /** Largura que o logo ocupa numa dada altura. */
        static int widthForHeight (int h)
        {
            auto src = source();
            if (! src.isValid() || src.getHeight() == 0) return h * 4;
            return juce::roundToInt ((float) h * (float) src.getWidth() / (float) src.getHeight());
        }

        void paint (juce::Graphics& g) override
        {
            auto src = source();
            if (! src.isValid()) return;

            const int h = getHeight();
            if (h <= 0) return;

            if (! cached.isValid() || cached.getHeight() != h)
                cached = downscale (src, widthForHeight (h), h);

            g.drawImageAt (cached, getWidth() - cached.getWidth(), (getHeight() - cached.getHeight()) / 2);
        }

    private:
        /** Redução em etapas (halving) — evita o serrilhado de reduzir 20x de uma vez. */
        static juce::Image downscale (juce::Image img, int w, int h)
        {
            while (img.getWidth() >= w * 2 && img.getHeight() >= h * 2)
                img = img.rescaled (img.getWidth() / 2, img.getHeight() / 2, juce::Graphics::highResamplingQuality);

            return img.rescaled (juce::jmax (1, w), juce::jmax (1, h), juce::Graphics::highResamplingQuality);
        }

        static juce::Image source()
        {
            return juce::ImageCache::getFromMemory (BinaryData::logoheader_png, BinaryData::logoheader_pngSize);
        }

        juce::Image cached;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LogoBadge)
    };
}

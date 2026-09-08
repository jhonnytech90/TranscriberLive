#include "Hub.h"
#include "BinaryData.h"

namespace tl
{
    const char* Hub::pageHtml()    { return BinaryData::display_html; }
    int         Hub::pageHtmlSize() { return BinaryData::display_htmlSize; }
}

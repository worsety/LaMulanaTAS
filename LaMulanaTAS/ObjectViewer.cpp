#include "LaMulanaTAS.h"

bool ObjectViewer::ProcessKeys()
{
    return false;
}

void ObjectViewer::Draw()
{
    // The majority of space will be taken up by the arguments and locals
    // Fortunately there are half as many pointers as other arguments because they need more space
    // Since there are 32 rows of text allocated for the additional overlay, using columns simplifies the layout
    auto &font8x12 = tas.font8x12;
    float x = OVERLAY_LEFT, y = OVERLAY_TOP;
}
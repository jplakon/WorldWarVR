// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "xr_types.h"
#include <array>
#include <cstdint>
namespace wawvr::mod {
// Exact common lens surface from three independently captured stock models.
// Their owner bones differ and are validated separately; this is not an alias.
inline constexpr std::array<wawvr::xr::Vec3f,21> kReviewedPtrsLensPoints{{
    {-2.415368F,0.651553F,3.496552F},
    {-2.777846F,0.0F,3.70464F},
    {-2.417461F,0.554244F,3.308832F},
    {-2.41457F,0.685083F,3.70464F},
    {-2.415368F,0.651552F,3.912728F},
    {-2.417461F,0.554244F,4.100447F},
    {-2.420057F,0.402682F,4.249422F},
    {-2.422164F,0.211702F,4.34507F},
    {-2.422971F,0.0F,4.378027F},
    {-2.422164F,-0.211702F,4.34507F},
    {-2.420057F,-0.402682F,4.249423F},
    {-2.417461F,-0.554244F,4.100447F},
    {-2.420057F,0.402682F,3.159857F},
    {-2.415368F,-0.651553F,3.912728F},
    {-2.41457F,-0.685083F,3.70464F},
    {-2.415368F,-0.651553F,3.496552F},
    {-2.417461F,-0.554244F,3.308832F},
    {-2.420057F,-0.402682F,3.159857F},
    {-2.422164F,-0.211702F,3.06421F},
    {-2.42297F,0.0F,3.031252F},
    {-2.422164F,0.211702F,3.06421F},
}};
inline constexpr std::array<std::array<std::uint16_t,3>,20> kReviewedPtrsLensTriangles{{
    {0,1,2},{3,1,0},{4,1,3},{5,1,4},{6,1,5},
    {7,1,6},{8,1,7},{9,1,8},{10,1,9},{11,1,10},
    {2,1,12},{13,1,11},{14,1,13},{15,1,14},{16,1,15},
    {17,1,16},{18,1,17},{19,1,18},{20,1,19},{12,1,20},
}};
} // namespace wawvr::mod

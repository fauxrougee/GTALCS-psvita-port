#pragma once

// Physical buttons, before configurable gameplay actions or touch triggers.
namespace PadCheats {
enum Button {
    Triangle=1u<<0, Circle=1u<<1, Cross=1u<<2, Square=1u<<3,
    Up=1u<<4, Down=1u<<5, Left=1u<<6, Right=1u<<7,
    L=1u<<8, R=1u<<9
};
}

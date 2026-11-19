#include "core/Ampacity.h"

#include <algorithm>
#include <cmath>
#include <regex>

namespace cartesio::plant {

namespace {

// Sezioni delle tabelle (mm²).
constexpr double kSections[] = {1.5, 2.5, 4, 6, 10, 16, 25, 35, 50, 70, 95, 120, 150, 185, 240, 300};
constexpr int kRows = 16;
// Colonne: A1, A2, B1, B2, C, E. 0 = non in tabella.
using Table = double[kRows][6];

// PVC, 2 conduttori caricati.
const Table kPvc2 = {
    {14.5, 14, 17.5, 16.5, 19.5, 22},  {19.5, 18.5, 24, 23, 27, 30},   {26, 25, 32, 30, 36, 40},       {34, 32, 41, 38, 46, 51},
    {46, 43, 57, 52, 63, 70},          {61, 57, 76, 69, 85, 94},       {80, 75, 101, 90, 112, 119},    {99, 92, 125, 111, 138, 148},
    {119, 110, 151, 133, 168, 180},    {151, 139, 192, 168, 213, 232}, {182, 167, 232, 201, 258, 282}, {210, 192, 269, 232, 299, 328},
    {0, 0, 0, 0, 0, 0},                {0, 0, 0, 0, 0, 0},             {0, 0, 0, 0, 0, 0},             {0, 0, 0, 0, 0, 0},
};
// PVC, 3 conduttori caricati.
const Table kPvc3 = {
    {13.5, 13, 15.5, 15, 17.5, 18.5}, {18, 17.5, 21, 20, 24, 25},      {24, 23, 28, 27, 32, 34},       {31, 29, 36, 34, 41, 43},
    {42, 39, 50, 46, 57, 60},         {56, 52, 68, 62, 76, 80},        {73, 68, 89, 80, 96, 101},      {89, 83, 110, 99, 119, 126},
    {108, 99, 134, 118, 144, 153},    {136, 125, 171, 149, 184, 196},  {164, 150, 207, 179, 223, 238}, {188, 172, 239, 206, 259, 276},
    {216, 196, 275, 236, 299, 319},   {245, 223, 314, 268, 341, 364},  {286, 261, 370, 313, 403, 430}, {328, 298, 426, 358, 464, 497},
};
// EPR/XLPE, 2 conduttori caricati.
const Table kEpr2 = {
    {19, 18.5, 23, 22, 24, 26},       {26, 25, 31, 30, 33, 36},        {35, 33, 42, 40, 45, 49},       {45, 42, 54, 51, 58, 63},
    {61, 57, 75, 69, 80, 86},         {81, 76, 100, 91, 107, 115},     {106, 99, 133, 119, 138, 149},  {131, 121, 164, 146, 171, 185},
    {158, 145, 198, 175, 209, 225},   {200, 183, 253, 221, 269, 289},  {241, 220, 306, 265, 328, 352}, {278, 253, 354, 305, 382, 410},
    {0, 0, 0, 0, 0, 0},               {0, 0, 0, 0, 0, 0},              {0, 0, 0, 0, 0, 0},             {0, 0, 0, 0, 0, 0},
};
// EPR/XLPE, 3 conduttori caricati.
const Table kEpr3 = {
    {17, 16.5, 20, 19.5, 22, 23},     {23, 22, 28, 26, 30, 32},        {31, 30, 37, 35, 40, 42},       {40, 38, 48, 44, 52, 54},
    {54, 51, 66, 60, 71, 75},         {73, 68, 88, 80, 96, 100},       {95, 89, 117, 105, 119, 127},   {117, 109, 144, 128, 147, 158},
    {141, 130, 175, 154, 179, 192},   {179, 164, 222, 194, 229, 246},  {216, 197, 269, 233, 278, 298}, {249, 227, 312, 268, 322, 346},
    {285, 259, 358, 307, 371, 399},   {324, 295, 408, 349, 424, 456},  {380, 346, 481, 407, 500, 538}, {435, 396, 553, 465, 576, 621},
};

} // namespace

const char* methodName(Method m)
{
    switch (m) {
    case Method::A1: return "A1";
    case Method::A2: return "A2";
    case Method::B1: return "B1";
    case Method::B2: return "B2";
    case Method::C: return "C";
    default: return "E";
    }
}

Insulation insulationOf(const std::string& cableType)
{
    static const std::regex pvc(R"(FS17|N07V|H07V|H05V|FROR|N1VV|FR2OR|FROH2R|N07G9|^PVC)", std::regex::ECMAScript | std::regex::icase);
    return std::regex_search(cableType, pvc) ? Insulation::PVC : Insulation::EPR;
}

std::optional<double> tableAmpacity(Insulation insulation, Method method, int loadedConductors, double section)
{
    const Table& t = insulation == Insulation::PVC ? (loadedConductors >= 3 ? kPvc3 : kPvc2) : (loadedConductors >= 3 ? kEpr3 : kEpr2);
    for (int r = 0; r < kRows; ++r)
        if (std::abs(kSections[r] - section) < 1e-6) {
            const double v = t[r][static_cast<int>(method)];
            if (v > 0.0)
                return v;
            return std::nullopt;
        }
    return std::nullopt;
}

double temperatureFactor(Insulation insulation, double ambientC)
{
    struct Row { double t, pvc, epr; };
    static const Row rows[] = {{10, 1.22, 1.15}, {15, 1.17, 1.12}, {20, 1.12, 1.08}, {25, 1.06, 1.04}, {30, 1.00, 1.00}, {35, 0.94, 0.96},
                               {40, 0.87, 0.91}, {45, 0.79, 0.87}, {50, 0.71, 0.82}, {55, 0.61, 0.76}, {60, 0.50, 0.71}};
    // Il valore della riga più vicina per eccesso (cautelativo).
    for (const Row& r : rows)
        if (ambientC <= r.t + 1e-9)
            return insulation == Insulation::PVC ? r.pvc : r.epr;
    return insulation == Insulation::PVC ? 0.50 : 0.71;
}

double trayGroupingFactor(int circuits)
{
    static const double k[] = {1.00, 1.00, 0.88, 0.82, 0.77, 0.75, 0.73, 0.73, 0.72, 0.72};
    return circuits <= 0 ? 1.0 : k[std::min(circuits, 9)];
}

} // namespace cartesio::plant

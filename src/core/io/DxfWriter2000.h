#pragma once

#include "core/Document.h"

#include <ostream>
#include <string>
#include <vector>

namespace cartesio::detail {

// Scrittura in DXF 2000 (AC1015). Usata da writeDxf().
void writeDxf2000(const Document& doc, std::ostream& out, std::vector<std::string>* warnings, bool forLibreDwg = false);

// Converte i caratteri non ASCII di una stringa UTF-8 in sequenze \U+XXXX (comprese dagli altri CAD).
std::string escapeNonAscii(const std::string& utf8);

} // namespace cartesio::detail

#pragma once

#include <QString>

namespace docx {

struct Package;

// "Code" paragraph style and "Inline Code" character style. Word has no native
// code block, so they are ordinary named styles: monospace, shaded, a thin left
// rule on blocks, no proofing. Added to the package's styles.xml the first time
// they are used, so Word and LibreOffice show them the same way.
namespace code {

inline const char *kBlockStyle = "Code";
inline const char *kInlineStyle = "CodeChar";
inline const char *kFont = "Consolas";  // substitutes: Menlo, Cousine, DejaVu Sans Mono, Liberation Mono, Courier New

// Make sure both styles exist in pkg (styles.xml text and parsed styles). Returns false if
// the package has no usable styles part and one couldn't be created.
bool ensureStyles(Package &pkg);

} // namespace code
} // namespace docx

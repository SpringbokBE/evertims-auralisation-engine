/* =========================================================================================

   This is an auto-generated file: Any edits you make may be overwritten!

*/

#pragma once

namespace BinaryData
{
    extern const char*   Doxyfile;
    const int            DoxyfileSize = 81463;

    extern const char*   LICENSE_md;
    const int            LICENSE_mdSize = 1758;

    extern const char*   README_md;
    const int            README_mdSize = 3719;

    extern const char*   evertims_logo_512_png;
    const int            evertims_logo_512_pngSize = 165475;

    extern const char*   evertims_logo_256_png;
    const int            evertims_logo_256_pngSize = 54544;

    // Number of elements in the namedResourceList and originalFileNames arrays.
    const int namedResourceListSize = 5;

    // Points to the start of a list of resource names.
    extern const char* namedResourceList[];

    // Points to the start of a list of resource filenames.
    extern const char* originalFilenames[];

    // If you provide the name of one of the binary resource variables above, this function will
    // return the corresponding data and its size (or a null pointer if the name isn't found).
    const char* getNamedResource (const char* resourceNameUTF8, int& dataSizeInBytes);

    // If you provide the name of one of the binary resource variables above, this function will
    // return the corresponding original, non-mangled filename (or a null pointer if the name isn't found).
    const char* getNamedResourceOriginalFilename (const char* resourceNameUTF8);
}

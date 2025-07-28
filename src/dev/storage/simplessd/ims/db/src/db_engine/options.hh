#ifndef __OPTIONS__HH__
#define __OPTIONS__HH__
enum class PackingType {
    kKeyPerPage     = 0x0,
    kHash           = 0x1,
    kKeyRange       = 0x2,
};


#define PACKING_T (PackingType::kKeyPerPage);
#endif  // __OPTIONS__HH__
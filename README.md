# open-l2asmdisasm

See [`ddf`](ddf) for supported DAT definitions.

## Usage

Make sure you have [re2c](https://re2c.org) installed.

In `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.28)
project(example VERSION 1.0.0 LANGUAGES CXX)

include(FetchContent)

fetchcontent_declare(
    l2asmdisasm
    GIT_REPOSITORY https://github.com/ritsuwastaken/open-l2asmdisasm.git
    GIT_TAG 0.1.0
)
fetchcontent_makeavailable(l2asmdisasm)

add_library(${PROJECT_NAME} src/main.cpp) # or add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE l2asmdisasm::l2asmdisasm)
```

In your code:

```cpp
#include <l2asmdisasm.h>
#include <iostream>

int main()
{
    const auto schema = l2asmdisasm::parse_ddf("{ UINT id; UNICODE name; }");
    const auto bytes = l2asmdisasm::assemble("id\tname\n1\tExample item\n", schema);
    std::cout << l2asmdisasm::disassemble(bytes, schema);
    // id    name
    // 1     Example item
}
```

See [`include/l2asmdisasm.h`](include/l2asmdisasm.h) for the public API.  
See [`cli`](./cli) for extended usage examples.

## Credits

- **DStuff** - [l2asm-disasm](https://web.archive.org/web/20111021065705/http://dstuff.luftbrandzlung.org/l2.php)
- [Hint](https://github.com/Hint-ru) - [l2disasm Glory Days+ patch](https://web.archive.org/web/20261101143448/https://forum.zone-game.info/showthread.php?tid=24654)
- **L2Miko** - [L2FileEdit](https://github.com/L2Miko/L2FileEdit) DAT definitions
- [gyo](https://github.com/gyod) - Helios and Underground DAT definitions
- **SQLite** - [lemon](https://sqlite.org/src/doc/trunk/doc/lemon.html)
- **re2c** - [re2c](https://re2c.org)

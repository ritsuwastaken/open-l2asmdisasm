# open-l2asmdisasm

See [`definitions`](definitions) for supported files and client versions and [`JSON.md`](JSON.md) for the format.

### [Download](https://github.com/ritsuwastaken/open-l2asmdisasm/releases/latest)

## Usage

```cmake
cmake_minimum_required(VERSION 3.28)
project(example LANGUAGES CXX)

include(FetchContent)

fetchcontent_declare(
    l2asmdisasm
    GIT_REPOSITORY https://github.com/ritsuwastaken/open-l2asmdisasm.git
    GIT_TAG 0.6.0
)
fetchcontent_makeavailable(l2asmdisasm)

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE l2asmdisasm::l2asmdisasm)
```

```cpp
#include <l2asmdisasm.h>
#include <iostream>

int main()
{
    const auto definition = R"({
        "blocks": [{"fields": [
            {"name": "id", "type": "u32"},
            {"name": "name", "type": "utf16"}
        ]}]
    })";
    const auto bytes = l2asmdisasm::assemble(R"([{"id": 1, "name": "Example item"}])", definition);
    std::cout << l2asmdisasm::disassemble(bytes, definition);
    // [{"id": 1, "name": "Example item"}]
}
```

See [`include/l2asmdisasm.h`](include/l2asmdisasm.h) for the public API.  
See [`cli`](./cli) for extended usage examples.

## Credits

- **DStuff** - [l2asm-disasm](https://web.archive.org/web/20111021065705/http://dstuff.luftbrandzlung.org/l2.php)
- **Hint** - [l2disasm GD+ patch](https://web.archive.org/web/20261101143448/https://forum.zone-game.info/showthread.php?tid=24654)
- [L2Miko](https://github.com/L2Miko), [gyo](https://github.com/gyod), **Allengc**, **Lin_syncia** - [L2FileEdit](https://github.com/L2Miko/L2FileEdit) DAT definitions
- **MobiusDevelopment** - [L2ClientDat](https://github.com/MobiusDevelopment/L2ClientDat) DAT definitions

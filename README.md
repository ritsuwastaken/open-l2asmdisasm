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
    GIT_TAG 0.9.1
)
fetchcontent_makeavailable(l2asmdisasm)

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE l2asmdisasm::l2asmdisasm)
```

```cpp
#include <l2asmdisasm.h>
#include <iostream>
#include <sstream>

int main()
{
    const auto schema = l2asmdisasm::parse_schema(R"({
        "blocks": [{"fields": [
            {"name": "id", "type": "u32"},
            {"name": "name", "type": "utf16"}
        ]}]
    })");
    std::ostringstream dat;
    l2asmdisasm::assemble(R"([{"id": 1, "name": "Example item"}])", schema, dat);
    const auto text = dat.str();
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    l2asmdisasm::disassemble(bytes, schema, std::cout);
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
- **Cryme** - [l2w_tool](https://github.com/Cryme/l2w_tool) DAT field names

# Build dependencies

Worldforge does not vendor SDL3 or Dear ImGui source files. By default, CMake fetches these upstream projects at the following tags if suitable local packages/source trees are not supplied.

| Dependency | Pinned tag | Upstream | License |
|---|---|---|---|
| SDL | `release-3.2.10` | https://github.com/libsdl-org/SDL | zlib license (see upstream repository) |
| Dear ImGui | `v1.92.5` | https://github.com/ocornut/imgui | MIT license (see upstream repository) |

If you redistribute a built application, review the licenses and notices for the exact dependency versions and any platform libraries included with your distribution. The dependencies' licenses are maintained by their respective upstream projects; Worldforge does not copy their source into this repository.

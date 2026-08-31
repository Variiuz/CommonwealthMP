# CommonwealthMP plugin

F4SE 0.7.9, CommonLibF4, Address Library for Steam **1.11.240**. Protocol 6.

```
git submodule update --init --recursive
xmake f -m releasedbg -a x64
xmake build
```

Output: `plugin/build/windows/x64/releasedbg/CommonwealthMP.dll`.

Console: `cmp_status`, `cmp_join 127 0 0 1 7777`, `cmp_query`, `cmp_leave`, `cmp_lobby`, `cmp_dump`.

Host: load any in-world save, then join. Guests can Join Server from the title (needs a live Commonwealth host). Dumps and crash files go in `Documents\My Games\Fallout4\F4SE\`.

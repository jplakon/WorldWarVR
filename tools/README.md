# Development tools

`map_t4_profiles.py` is an optional, local-only inspection aid for authorized
World at War executables. It does not run as part of the launcher, injected
DLL, package build, or payload audit.

Install its pinned Python dependencies with:

```powershell
python -m pip install -r tools\requirements-map_t4_profiles.txt
```

The tool reads the input executable without modifying it. Never commit its
input, Python bytecode cache, or generated inspection output.

import sys
import os
import json
import zipfile
import argparse
import shutil
import re
from pathlib import Path, PurePosixPath
from typing import Optional

# Switch to project root directory
os.chdir(Path(__file__).resolve().parent.parent)

################################################################################
# Common utility functions
################################################################################

def get_board_type_from_compile_commands() -> Optional[str]:
    """Parse the current compiled BOARD_TYPE from build/compile_commands.json"""
    compile_file = Path("build/compile_commands.json")
    if not compile_file.exists():
        return None
    with compile_file.open(encoding='utf-8') as f:
        data = json.load(f)
    for item in data:
        if not item["file"].endswith("main.cc"):
            continue
        cmd = item["command"]
        if "-DBOARD_TYPE=\\\"" in cmd:
            return cmd.split("-DBOARD_TYPE=\\\"")[1].split("\\\"")[0].strip()
    return None


def get_project_version() -> Optional[str]:
    """Read set(PROJECT_VER "x.y.z") from root CMakeLists.txt"""
    with Path("CMakeLists.txt").open(encoding='utf-8') as f:
        for line in f:
            if line.startswith("set(PROJECT_VER"):
                return line.split("\"")[1]
    return None


def merge_bin() -> None:
    if os.system("idf.py merge-bin") != 0:
        print("merge-bin failed", file=sys.stderr)
        sys.exit(1)


def zip_bin(name: str, version: str) -> None:
    """Zip build/merged-binary.bin to releases/{name}/v{version}/v{version}_{name}.zip"""
    out_dir = Path(f"releases/{name}/v{version}")
    out_dir.mkdir(parents=True, exist_ok=True)
    output_path = out_dir / f"v{version}_{name}.zip"

    if output_path.exists():
        output_path.unlink()

    with zipfile.ZipFile(output_path, "w", compression=zipfile.ZIP_DEFLATED) as zipf:
        zipf.write("build/merged-binary.bin", arcname="merged-binary.bin")

    version_dir = Path(f"releases/v{version}")
    version_dir.mkdir(parents=True, exist_ok=True)
    output_path = version_dir / f"v{version}_{name}.zip"

    if output_path.exists():
        output_path.unlink()

    with zipfile.ZipFile(output_path, "w", compression=zipfile.ZIP_DEFLATED) as zipf:
        zipf.write("build/merged-binary.bin", arcname="merged-binary.bin")
    print(f"zip bin to {output_path} done")

    ota_dir = Path(f"releases/v{version}-ota")
    ota_dir.mkdir(parents=True, exist_ok=True)
    bin_src = Path("build/houzzkit.bin")
    if bin_src.exists():
        bin_dst = ota_dir / f"{name}_v{version}.bin"
        try:
            shutil.copy2(bin_src, bin_dst)  # 保留文件元数据
            print(f"copy {bin_src} to {bin_dst} done")
        except Exception as e:
            print(f"Error copying file: {e}")


def _get_manufacturer(cfg: dict) -> Optional[str]:
    """Read manufacturer from config.json"""
    m = cfg.get("manufacturer")
    if isinstance(m, str) and m.strip():
        return m.strip()
    return None


def _get_full_name(name: str, manufacturer: Optional[str]) -> str:
    """Prefix variant name with manufacturer unless it already has that prefix."""
    if manufacturer and not name.startswith(f"{manufacturer}-"):
        return f"{manufacturer}-{name}"
    return name


################################################################################
# board / variant related functions
################################################################################

_BOARDS_DIR = Path("main/boards")


def _normalize_board_path(board_type: str) -> str:
    """Normalize user input or relative board path to POSIX style."""
    return board_type.replace("\\", "/").strip("/")


def _get_board_leaf(board_type: str) -> str:
    """Return the CMake BOARD_TYPE leaf from a relative board path."""
    return PurePosixPath(_normalize_board_path(board_type)).name


def _get_board_parent(board_type: str) -> Optional[str]:
    """Return the relative parent folder for a board path, if any."""
    parent = PurePosixPath(_normalize_board_path(board_type)).parent.as_posix()
    return None if parent == "." else parent


def _get_board_rel_path(cfg_path: Path) -> str:
    """Return config directory path relative to main/boards in POSIX style."""
    return cfg_path.parent.relative_to(_BOARDS_DIR).as_posix()


def _validate_manufacturer(cfg_path: Path, board: str, manufacturer: Optional[str], errors: list[str]) -> None:
    """Validate manufacturer and directory structure without assuming chip folders."""
    board_parts = board.split("/")
    if len(board_parts) > 1:
        expected_manufacturer = board_parts[0]
        if not manufacturer:
            errors.append(
                f"{cfg_path}: Board is under '{expected_manufacturer}/', "
                f"but config.json is missing \"manufacturer\": \"{expected_manufacturer}\""
            )
        elif manufacturer != expected_manufacturer:
            errors.append(
                f"{cfg_path}: manufacturer mismatch, "
                f"directory is '{expected_manufacturer}/' but config.json has \"{manufacturer}\""
            )
    elif manufacturer:
        errors.append(
            f"{cfg_path}: Board is not in a manufacturer subdirectory, "
            f"but config.json defines manufacturer \"{manufacturer}\", "
            f"please move board to main/boards/{manufacturer}/{board}/"
        )


def _find_board_config_path(board_type: str, config_filename: str = "config.json") -> tuple[Path, str]:
    """Resolve board input to config path and board directory relative to main/boards."""
    normalized = _normalize_board_path(board_type)
    direct_cfg = _BOARDS_DIR / Path(normalized) / config_filename
    if direct_cfg.exists():
        return direct_cfg, _get_board_rel_path(direct_cfg)

    matches: list[tuple[Path, str]] = []
    for cfg_path in _BOARDS_DIR.rglob(config_filename):
        board = _get_board_rel_path(cfg_path)
        if board == normalized or _get_board_leaf(board) == normalized:
            matches.append((cfg_path, board))

    if not matches:
        raise FileNotFoundError(f"Board {board_type} has no {config_filename} config file")
    if len(matches) > 1:
        candidates = ", ".join(board for _, board in matches)
        raise ValueError(f"Board type {board_type} is ambiguous, candidates: {candidates}")
    return matches[0]


def _collect_variants(config_filename: str = "config.json") -> list[dict[str, str]]:
    """Traverse all boards under main/boards, collect variant information.

    Return example:
        [{"board": "bread-compact-ml307", "path": "bread-compact-ml307", "name": "bread-compact-ml307", "full_name": "bread-compact-ml307"}, ...]
        [{"board": "esp32-p4-nano", "path": "waveshare/esp32-p4-nano", "name": "esp32-p4-nano-10.1-a", "full_name": "waveshare-esp32-p4-nano-10.1-a"}, ...]
    """
    variants: list[dict[str, str]] = []
    errors: list[str] = []

    for cfg_path in _BOARDS_DIR.rglob(config_filename):
        board_dir = cfg_path.parent
        if board_dir.name == "common":
            continue
        board = _get_board_rel_path(cfg_path)

        try:
            with cfg_path.open(encoding='utf-8') as f:
                cfg = json.load(f)

            manufacturer = _get_manufacturer(cfg)
            _validate_manufacturer(cfg_path, board, manufacturer, errors)

            for build in cfg.get("builds", []):
                name = build["name"]
                full_name = _get_full_name(name, manufacturer)
                variants.append({
                    "board": _get_board_leaf(board),
                    "path": board,
                    "name": name,
                    "full_name": full_name
                })

        except Exception as e:
            print(f"[ERROR] Failed to parse {cfg_path}: {e}", file=sys.stderr)

    # Report all errors at once
    if errors:
        print("\n[ERROR] Found manufacturer configuration issues:", file=sys.stderr)
        for err in errors:
            print(f"  - {err}", file=sys.stderr)
        print(file=sys.stderr)
        sys.exit(1)

    return variants


def _find_board_config_candidates(board_type: str) -> list[str]:
    """Find all CONFIG_BOARD_TYPE_xxx candidates for the given board_type."""
    board_leaf = _get_board_leaf(board_type)
    pattern = f'set(BOARD_TYPE "{board_leaf}")'

    cmake_file = Path("main/CMakeLists.txt")
    lines = cmake_file.read_text(encoding="utf-8").splitlines()
    candidates: list[str] = []

    for idx, line in enumerate(lines):
        if pattern in line:
            # Found the BOARD_TYPE line, search backwards for the nearest config guard
            for back_idx in range(idx - 1, -1, -1):
                back_line = lines[back_idx]
                if "if(CONFIG_BOARD_TYPE_" in back_line:
                    candidates.append(back_line.strip().split("if(")[1].split(")")[0])
                    break
    return candidates


def _extract_board_config_from_sdkconfig_append(sdkconfig_append: list[str]) -> Optional[str]:
    """Extract explicit CONFIG_BOARD_TYPE_xxx=y from sdkconfig_append, if present."""
    pattern = re.compile(r"^(CONFIG_BOARD_TYPE_[A-Z0-9_]+)=y$")
    matches = []
    for item in sdkconfig_append:
        m = pattern.match(item.strip())
        if m:
            matches.append(m.group(1))
    if not matches:
        return None
    uniq = list(dict.fromkeys(matches))
    if len(uniq) > 1:
        raise ValueError(f"Multiple board type configs found in sdkconfig_append: {uniq}")
    return uniq[0]


def _symbol_supports_target(symbol: str, target: str) -> bool:
    """Check whether Kconfig symbol depends on given target (e.g. esp32c5)."""
    kconfig_file = Path("main/Kconfig.projbuild")
    if not kconfig_file.exists():
        return False

    target_flag = f"IDF_TARGET_{target.upper()}"
    lines = kconfig_file.read_text(encoding="utf-8").splitlines()

    in_symbol = False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("config "):
            curr_symbol = stripped.split("config ", 1)[1].strip()
            in_symbol = curr_symbol == symbol
            continue
        if in_symbol and stripped.startswith(("config ", "choice ", "endchoice", "menu ", "endmenu")):
            break
        if in_symbol and "depends on" in stripped and target_flag in stripped:
            return True
    return False


def _resolve_board_config(board_type: str, target: str, sdkconfig_append: list[str]) -> str:
    """Resolve CONFIG_BOARD_TYPE_xxx for current board build."""
    explicit = _extract_board_config_from_sdkconfig_append(sdkconfig_append)
    if explicit:
        return explicit

    candidates = _find_board_config_candidates(board_type)
    if not candidates:
        raise ValueError(f"Cannot find board config symbol for {board_type}")
    if len(candidates) == 1:
        return candidates[0]

    by_target = [c for c in candidates if _symbol_supports_target(c, target)]
    if len(by_target) == 1:
        return by_target[0]
    if len(by_target) > 1:
        selected = by_target[0]
        print(
            f"[WARN] Ambiguous board config for {board_type} (target={target}), "
            f"target-matched candidates={by_target}, selecting first: {selected}",
            file=sys.stderr,
        )
        return selected

    target_u = target.upper()
    target_short = target_u.replace("ESP32", "")
    by_name = [
        c for c in candidates
        if target_u in c or f"_{target_short}" in c
    ]
    if len(by_name) == 1:
        return by_name[0]
    if len(by_name) > 1:
        selected = by_name[0]
        print(
            f"[WARN] Ambiguous board config for {board_type} (target={target}), "
            f"name-matched candidates={by_name}, selecting first: {selected}",
            file=sys.stderr,
        )
        return selected

    selected = candidates[0]
    print(
        f"[WARN] Ambiguous board config for {board_type} (target={target}), "
        f"candidates={candidates}, selecting first: {selected}",
        file=sys.stderr,
    )
    return selected


# Kconfig "select" entries are not automatically applied when we simply append
# sdkconfig lines from config.json, so add the required dependencies here to
# mimic menuconfig behaviour.
_AUTO_SELECT_RULES: dict[str, list[str]] = {
    "CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING": [
        "CONFIG_BT_ENABLED=y",
        "CONFIG_BT_BLUEDROID_ENABLED=y",
        "CONFIG_BT_BLE_42_FEATURES_SUPPORTED=y",
        "CONFIG_BT_BLE_50_FEATURES_SUPPORTED=n",
        "CONFIG_BT_BLE_BLUFI_ENABLE=y",
        "CONFIG_MBEDTLS_DHM_C=y",
    ],
}


def _apply_auto_selects(sdkconfig_append: list[str]) -> list[str]:
    """Apply hardcoded auto-select rules to sdkconfig_append."""
    items: list[str] = []
    existing_keys: set[str] = set()

    def _append_if_missing(entry: str) -> None:
        key = entry.split("=", 1)[0]
        if key not in existing_keys:
            items.append(entry)
            existing_keys.add(key)

    # Preserve original order while tracking keys
    for entry in sdkconfig_append:
        _append_if_missing(entry)

    # Apply auto-select rules
    for key, deps in _AUTO_SELECT_RULES.items():
        for entry in sdkconfig_append:
            name, _, value = entry.partition("=")
            if name == key and value.lower().startswith("y"):
                for dep in deps:
                    _append_if_missing(dep)
                break

    return items

################################################################################
# Check board_type in CMakeLists
################################################################################

def _board_type_exists(board_type: str) -> bool:
    cmake_file = Path("main/CMakeLists.txt").read_text(encoding="utf-8")
    board_leaf = _get_board_leaf(board_type)
    pattern = f'set(BOARD_TYPE "{board_leaf}")'
    return pattern in cmake_file

################################################################################
# Compile implementation
################################################################################

def release(board_type: str, config_filename: str = "config.json", *, filter_name: Optional[str] = None) -> None:
    """Compile and package all/specified variants of the specified board_type.

    Args:
        board_type: CMake BOARD_TYPE or directory path under main/boards
        config_filename: config.json name (default: config.json)
        filter_name: if specified, only compile the build["name"] that matches
    """
    try:
        cfg_path, board_path = _find_board_config_path(board_type, config_filename)
    except (FileNotFoundError, ValueError) as e:
        print(f"[ERROR] {e}", file=sys.stderr)
        sys.exit(1)

    project_version = get_project_version()
    print(f"Project Version: {project_version} ({cfg_path})")

    with cfg_path.open(encoding='utf-8') as f:
        cfg = json.load(f)
    target = cfg["target"]
    manufacturer = _get_manufacturer(cfg)
    cmake_board_type = _get_board_leaf(board_path)
    board_parent = _get_board_parent(board_path)

    builds = cfg.get("builds", [])
    if filter_name:
        builds = [b for b in builds if b["name"] == filter_name]
        if not builds:
            print(f"[ERROR] Variant {filter_name} not found in {board_path}'s {config_filename}", file=sys.stderr)
            sys.exit(1)

    for build in builds:
        name = build["name"]

        if cmake_board_type not in name:
            print(
                f"[WARN] build.name {name} does not contain board type {cmake_board_type}; "
                "keep existing release name for compatibility.",
                file=sys.stderr,
            )

        final_name = _get_full_name(name, manufacturer)
        output_path = Path(f"releases/{final_name}/v{project_version}/v{project_version}_{final_name}.zip")
        if output_path.exists():
            print(f"Skipping {final_name} because {output_path} already exists")
            continue

        # Process sdkconfig_append
        build_sdkconfig_append = build.get("sdkconfig_append", [])
        explicit_board_cfg = _extract_board_config_from_sdkconfig_append(build_sdkconfig_append)
        if explicit_board_cfg:
            print(
                f"[INFO] Board config explicitly set in config.json: {explicit_board_cfg}, "
                "skip auto-select.",
            )
            sdkconfig_append = list(build_sdkconfig_append)
        else:
            board_type_config = _resolve_board_config(cmake_board_type, target, build_sdkconfig_append)
            sdkconfig_append = [f"{board_type_config}=y"]
            sdkconfig_append.extend(build_sdkconfig_append)
        sdkconfig_append = _apply_auto_selects(sdkconfig_append)

        print("-" * 80)
        print(f"name: {final_name}")
        print(f"target: {target}")
        print(f"board_type: {cmake_board_type}")
        print(f"board_path: {board_path}")
        if manufacturer:
            print(f"manufacturer: {manufacturer}")
        for item in sdkconfig_append:
            print(f"sdkconfig_append: {item}")

        os.environ.pop("IDF_TARGET", None)

        # Call set-target
        if os.system(f"idf.py set-target {target}") != 0:
            print("set-target failed", file=sys.stderr)
            sys.exit(1)

        # Append sdkconfig
        with Path("sdkconfig").open("a", encoding='utf-8') as f:
            f.write("\n")
            f.write("# Append by release.py\n")
            for append in sdkconfig_append:
                f.write(f"{append}\n")
        # Build with macro BOARD_NAME defined to name
        manufacturer_arg = f" -DMANUFACTURER={board_parent}" if board_parent else ""
        build_cmd = f"idf.py -DBOARD_NAME={name} -DBOARD_TYPE={cmake_board_type}{manufacturer_arg} build"
        if os.system(build_cmd) != 0:
            print("build failed")
            sys.exit(1)

        # merge-bin
        merge_bin()

        # Zip
        zip_bin(final_name, project_version)

################################################################################
# CLI entry
################################################################################

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("board", nargs="?", default=None, help="Board type, board path, or 'all'")
    parser.add_argument("-c", "--config", default="config.json", help="Config filename (default: config.json)")
    parser.add_argument("--list-boards", action="store_true", help="List all supported boards and variants")
    parser.add_argument("--json", action="store_true", help="Output in JSON format (use with --list-boards)")
    parser.add_argument("--name", help="Variant name to compile (original name without manufacturer prefix)")

    args = parser.parse_args()

    # List mode
    if args.list_boards:
        variants = _collect_variants(config_filename=args.config)
        if args.json:
            print(json.dumps(variants))
        else:
            for v in variants:
                print(f"{v['path']}: {v['name']}")
        sys.exit(0)

    # Current directory firmware packaging mode
    if args.board is None:
        merge_bin()
        curr_board_type = get_board_type_from_compile_commands()
        if curr_board_type is None:
            print("Failed to parse board_type from compile_commands.json", file=sys.stderr)
            sys.exit(1)
        project_ver = get_project_version()
        zip_bin(curr_board_type, project_ver)
        sys.exit(0)

    # Compile mode
    board_type_input: str = args.board
    name_filter: Optional[str] = args.name

    # Check board_type in CMakeLists
    if board_type_input != "all" and not _board_type_exists(board_type_input):
        print(f"[ERROR] board_type {board_type_input} not found in main/CMakeLists.txt", file=sys.stderr)
        sys.exit(1)

    variants_all = _collect_variants(config_filename=args.config)

    # Filter board_type list
    target_board_types: set[str]
    filter_board_path: Optional[str] = None
    if board_type_input == "all":
        target_board_types = {v["path"] for v in variants_all}
    else:
        try:
            _, resolved_board_path = _find_board_config_path(board_type_input, args.config)
        except (FileNotFoundError, ValueError) as e:
            print(f"[ERROR] {e}", file=sys.stderr)
            sys.exit(1)
        filter_board_path = resolved_board_path
        target_board_types = {resolved_board_path}

    for bt in sorted(target_board_types):
        if not _board_type_exists(bt):
            print(f"[ERROR] board_type {bt} not found in main/CMakeLists.txt", file=sys.stderr)
            sys.exit(1)
        release(bt, config_filename=args.config, filter_name=name_filter if bt == filter_board_path else None)

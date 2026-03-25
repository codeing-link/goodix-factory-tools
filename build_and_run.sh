#!/bin/bash
# ============================================================
# build_and_run.sh — 自动构建并运行 GH Protocol 后端
# 使用方法:
#   chmod +x build_and_run.sh
#   ./build_and_run.sh           # 模拟器模式（无需设备）
#   ./build_and_run.sh --port /dev/ttyUSB0  # 真实设备
# ============================================================

set -e  # 任意命令失败即退出

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  GH Protocol 后端构建脚本"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  工程目录: $SCRIPT_DIR"
echo "  构建目录: $BUILD_DIR"
echo ""

# ── 1. 创建 build 目录 ──────────────────────────
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# ── 2. CMake 配置（自动下载 mongoose）──────────
echo "▶ 正在运行 cmake..."
cmake .. \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
echo ""

# ── 3. 编译 ─────────────────────────────────────
echo "▶ 正在编译..."
make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
echo ""

# ── 4. 验证二进制 ────────────────────────────────
if [ ! -f "$BUILD_DIR/gh_backend" ]; then
  echo "✗ 编译失败，未找到 gh_backend"
  exit 1
fi
echo "✓ 编译成功: $BUILD_DIR/gh_backend"
echo ""

# ── 5. 启动服务器 ────────────────────────────────
cd "$SCRIPT_DIR"  # 回到工程根目录（前端路径 web/frontend 相对于此）
exec "$BUILD_DIR/gh_backend" "$@"

RED=$'\033[0;31m'
GREEN=$'\033[0;32m'
YELLOW=$'\033[0;33m'
BLUE=$'\033[0;34m'
CYAN=$'\033[0;36m'
NC=$'\033[0m'

export LOG_LEVEL="${LOG_LEVEL:-2}"

log_debug()   { [ "$LOG_LEVEL" -ge 3 ] && printf "%s[DEBUG]%s %s\n" "$CYAN" "$NC" "$1" || true; }
log_info()    { [ "$LOG_LEVEL" -ge 2 ] && printf "%s[INFO]%s %s\n" "$BLUE" "$NC" "$1" || true; }
log_success() { [ "$LOG_LEVEL" -ge 2 ] && printf "%s[SUCCESS]%s %s\n" "$GREEN" "$NC" "$1" || true; }
log_warn()    { [ "$LOG_LEVEL" -ge 1 ] && printf "%s[WARN]%s %s\n" "$YELLOW" "$NC" "$1" || true; }
log_error()   { [ "$LOG_LEVEL" -ge 0 ] && printf "%s[ERROR]%s %s\n" "$RED" "$NC" "$1" || true; }
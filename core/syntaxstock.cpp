// syntaxstock.cpp - the stock syntax packs. They are written to <home>/syntax/ on first launch
// (and refreshed when "revision" goes up and the file still says "builtin": true).
// Raw strings: JSON needs "\\" for every regex backslash (\\b, \\s, \\d ...). Format: Documentation/syntax.md
// Bump "revision" of a pack whenever you change it here.

static const char* const kC = R"JSON({
  "name": "C",
  "revision": 1,
  "builtin": true,
  "extensions": ["c", "h"],
  "words": {
    "keyword": ["auto", "break", "case", "continue", "default", "do", "else", "enum", "extern", "for", "goto", "if",
                "inline", "register", "restrict", "return", "sizeof", "static", "struct", "switch", "typedef", "union",
                "volatile", "while", "typeof", "alignas", "alignof", "static_assert", "thread_local",
                "_Alignas", "_Alignof", "_Atomic", "_Generic", "_Noreturn", "_Static_assert", "_Thread_local"],
    "type": ["void", "char", "short", "int", "long", "float", "double", "signed", "unsigned", "const", "bool", "_Bool",
             "_Complex", "size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t", "int8_t", "int16_t", "int32_t",
             "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "wchar_t", "off_t", "FILE", "va_list"],
    "constant": ["NULL", "true", "false", "EOF", "stdin", "stdout", "stderr", "__FILE__", "__LINE__", "__func__",
                 "__DATE__", "__TIME__", "__VA_ARGS__"]
  },
  "regions": [
    {"scope": "comment", "begin": "/\\*", "end": "\\*/"},
    {"scope": "string",  "begin": "\"",   "end": "\"",   "escape": "\\", "multiline": false},
    {"scope": "string",  "begin": "'",    "end": "'",    "escape": "\\", "multiline": false}
  ],
  "rules": [
    {"scope": "comment",      "pattern": "//.*"},
    {"scope": "preprocessor", "pattern": "^\\s*#\\s*(?:include|import)\\s*<[^>\\n]*>"},
    {"scope": "preprocessor", "pattern": "^\\s*#\\s*\\w+"},
    {"scope": "type",         "pattern": "(?:(?<=struct )|(?<=enum )|(?<=union ))[A-Za-z_]\\w*"},
    {"scope": "number",       "pattern": "\\b(?:0[xX][0-9a-fA-F']+|0[bB][01']+|(?:\\d[\\d']*\\.?\\d*|\\.\\d+)(?:[eE][+-]?\\d+)?)[uUlLfFzZ]*\\b"},
    {"scope": "function",     "pattern": "\\b[A-Za-z_]\\w*(?=\\s*\\()"}
  ]
})JSON";

static const char* const kCpp = R"JSON({
  "name": "C++",
  "revision": 1,
  "builtin": true,
  "extensions": ["cpp", "cc", "cxx", "c++", "hpp", "hh", "hxx", "h++", "ipp", "tpp", "inl"],
  "words": {
    "keyword": ["alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "break", "case", "catch",
                "class", "compl", "concept", "const_cast", "consteval", "constexpr", "constinit", "continue",
                "co_await", "co_return", "co_yield", "decltype", "default", "delete", "do", "dynamic_cast", "else",
                "enum", "explicit", "export", "extern", "final", "for", "friend", "goto", "if", "inline", "mutable",
                "namespace", "new", "noexcept", "not", "not_eq", "operator", "or", "or_eq", "override", "private",
                "protected", "public", "register", "reinterpret_cast", "requires", "return", "sizeof", "static",
                "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local", "throw",
                "try", "typedef", "typeid", "typename", "union", "using", "virtual", "volatile", "while", "xor",
                "xor_eq"],
    "type": ["void", "bool", "char", "char8_t", "char16_t", "char32_t", "wchar_t", "short", "int", "long", "float",
             "double", "signed", "unsigned", "const", "size_t", "ssize_t", "ptrdiff_t", "nullptr_t", "int8_t",
             "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "intptr_t", "uintptr_t"],
    "constant": ["nullptr", "NULL", "true", "false", "__FILE__", "__LINE__", "__func__", "__cplusplus", "__VA_ARGS__"]
  },
  "regions": [
    {"scope": "comment", "begin": "/\\*",   "end": "\\*/"},
    {"scope": "string",  "begin": "R\"\\(", "end": "\\)\""},
    {"scope": "string",  "begin": "\"",     "end": "\"", "escape": "\\", "multiline": false},
    {"scope": "string",  "begin": "'",      "end": "'",  "escape": "\\", "multiline": false}
  ],
  "rules": [
    {"scope": "comment",      "pattern": "//.*"},
    {"scope": "preprocessor", "pattern": "^\\s*#\\s*(?:include|import)\\s*<[^>\\n]*>"},
    {"scope": "preprocessor", "pattern": "^\\s*#\\s*\\w+"},
    {"scope": "preprocessor", "pattern": "\\[\\[[^\\]\\n]*\\]\\]"},
    {"scope": "type",         "pattern": "(?:(?<=class )|(?<=struct )|(?<=enum )|(?<=union )|(?<=namespace ))[A-Za-z_]\\w*"},
    {"scope": "type",         "pattern": "\\bstd(?=::)"},
    {"scope": "number",       "pattern": "\\b(?:0[xX][0-9a-fA-F']+|0[bB][01']+|(?:\\d[\\d']*\\.?\\d*|\\.\\d+)(?:[eE][+-]?\\d+)?)[uUlLfFzZ]*\\b"},
    {"scope": "function",     "pattern": "\\b[A-Za-z_]\\w*(?=\\s*(?:<[^<>\\n]*>)?\\s*\\()"}
  ]
})JSON";

static const char* const kMakefile = R"JSON({
  "name": "Makefile",
  "revision": 1,
  "builtin": true,
  "extensions": ["mk", "mak", "make"],
  "filenames": ["Makefile", "makefile", "GNUmakefile"],
  "regions": [
    {"scope": "string", "begin": "\"", "end": "\"", "escape": "\\", "multiline": false},
    {"scope": "string", "begin": "'",  "end": "'",  "multiline": false}
  ],
  "rules": [
    {"scope": "comment",  "pattern": "(?<!\\\\)#.*"},
    {"scope": "keyword",  "pattern": "^\\s*(?:-?include|sinclude|ifn?eq|ifn?def|else|endif|define|endef|export|unexport|override|private|vpath|undefine)\\b"},
    {"scope": "function", "pattern": "^[^\\s#:=][^:=#\\n]*(?=:(?!=))"},
    {"scope": "variable", "pattern": "\\$\\$[A-Za-z_]\\w*"},
    {"scope": "variable", "pattern": "\\$\\((?:[^()\\n]|\\$\\([^()\\n]*\\))*\\)"},
    {"scope": "variable", "pattern": "\\$\\{[^}\\n]*\\}"},
    {"scope": "variable", "pattern": "\\$[@<^?*%+|]"},
    {"scope": "constant", "pattern": "^\\s*[A-Za-z_][\\w.\\-]*(?=\\s*(?::{1,3}|[+?!])?=)"}
  ]
})JSON";

static const char* const kLd = R"JSON({
  "name": "Linker script",
  "revision": 1,
  "builtin": true,
  "extensions": ["ld", "lds", "x", "ldscript"],
  "words": {
    "keyword": ["ENTRY", "SECTIONS", "MEMORY", "PHDRS", "VERSION", "OUTPUT_FORMAT", "OUTPUT_ARCH", "OUTPUT", "TARGET",
                "SEARCH_DIR", "INPUT", "GROUP", "AS_NEEDED", "STARTUP", "INCLUDE", "EXTERN", "NOCROSSREFS", "OPTION",
                "INSERT", "AFTER", "BEFORE", "OVERLAY", "NOLOAD", "AT", "FLAGS", "FILEHDR", "KEEP", "SORT",
                "SORT_BY_NAME", "SORT_BY_ALIGNMENT", "SORT_BY_INIT_PRIORITY", "SORT_NONE", "EXCLUDE_FILE",
                "CONSTRUCTORS", "HIDDEN", "PROVIDE", "PROVIDE_HIDDEN", "ASSERT", "REGION_ALIAS", "ONLY_IF_RO",
                "ONLY_IF_RW", "SUBALIGN", "ALIGN_WITH_INPUT", "FORCE_COMMON_ALLOCATION",
                "INHIBIT_COMMON_ALLOCATION", "FORCE_GROUP_ALLOCATION"],
    "builtin": ["ABSOLUTE", "ADDR", "ALIGN", "ALIGNOF", "BLOCK", "DATA_SEGMENT_ALIGN", "DATA_SEGMENT_END",
                "DATA_SEGMENT_RELRO_END", "DEFINED", "LENGTH", "LOADADDR", "LOG2CEIL", "MAX", "MIN", "NEXT", "ORIGIN",
                "SEGMENT_START", "SIZEOF", "SIZEOF_HEADERS", "CONSTANT", "BYTE", "SHORT", "LONG", "QUAD", "SQUAD",
                "FILL", "COMMON"]
  },
  "regions": [
    {"scope": "comment", "begin": "/\\*", "end": "\\*/"},
    {"scope": "string",  "begin": "\"",   "end": "\"", "multiline": false}
  ],
  "rules": [
    {"scope": "preprocessor", "pattern": "^\\s*#.*"},
    {"scope": "type",     "pattern": "(?<![\\w.])\\.[A-Za-z_][\\w.\\-]*"},
    {"scope": "constant", "pattern": "(?<![\\w.])\\.(?![\\w.])"},
    {"scope": "number",   "pattern": "\\b(?:0[xX][0-9a-fA-F]+|\\d+)[KkMm]?\\b"},
    {"scope": "variable", "pattern": "\\b_{1,2}[A-Za-z]\\w*\\b"}
  ]
})JSON";

static const char* const kRust = R"JSON({
  "name": "Rust",
  "revision": 1,
  "builtin": true,
  "extensions": ["rs"],
  "words": {
    "keyword": ["as", "async", "await", "break", "const", "continue", "crate", "dyn", "else", "enum", "extern", "fn",
                "for", "if", "impl", "in", "let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return",
                "self", "Self", "static", "struct", "super", "trait", "type", "unsafe", "use", "where", "while"],
    "type": ["i8", "i16", "i32", "i64", "i128", "isize", "u8", "u16", "u32", "u64", "u128", "usize", "f32", "f64",
             "bool", "char", "str"],
    "constant": ["true", "false", "None", "Some", "Ok", "Err"]
  },
  "regions": [
    {"scope": "comment", "begin": "/\\*", "end": "\\*/"},
    {"scope": "string",  "begin": "b?\"", "end": "\"", "escape": "\\"}
  ],
  "rules": [
    {"scope": "comment",   "pattern": "//.*"},
    {"scope": "string",    "pattern": "b?r(#*)\".*?\"\\1"},
    {"scope": "string",    "pattern": "b?'(?:\\\\(?:x[0-9a-fA-F]{2}|u\\{[0-9a-fA-F_]+\\}|.)|[^\\\\'\\n])'"},
    {"scope": "label",     "pattern": "'[A-Za-z_]\\w*(?!')"},
    {"scope": "attribute", "pattern": "#!?\\[[^\\]\\n]*\\]"},
    {"scope": "macro",     "pattern": "\\b[A-Za-z_]\\w*!(?=[\\s(\\[{])"},
    {"scope": "number",    "pattern": "\\b(?:0x[0-9a-fA-F_]+|0o[0-7_]+|0b[01_]+|\\d[\\d_]*(?:\\.\\d[\\d_]*)?(?:[eE][+-]?\\d+)?)(?:[iu](?:8|16|32|64|128|size)|f32|f64)?\\b"},
    {"scope": "function",  "pattern": "(?<=\\bfn )[A-Za-z_]\\w*"},
    {"scope": "function",  "pattern": "\\b[a-z_]\\w*(?=\\s*(?:::<[^>\\n]*>)?\\()"},
    {"scope": "constant",  "pattern": "\\b[A-Z][A-Z0-9_]{2,}\\b"},
    {"scope": "type",      "pattern": "\\b[A-Z][A-Za-z0-9_]*\\b"}
  ]
})JSON";

static const char* const kBash = R"JSON({
  "name": "Bash",
  "revision": 1,
  "builtin": true,
  "extensions": ["sh", "bash", "zsh", "ksh", "bashrc"],
  "filenames": [".bashrc", ".bash_profile", ".bash_aliases", ".bash_logout", ".profile", ".zshrc", "PKGBUILD"],
  "words": {
    "keyword": ["if", "then", "else", "elif", "fi", "case", "esac", "for", "select", "while", "until", "do", "done",
                "in", "function", "time", "coproc", "[[", "]]", "[", "]"],
    "builtin": ["echo", "printf", "read", "cd", "pwd", "export", "unset", "local", "declare", "typeset", "readonly",
                "return", "exit", "shift", "set", "shopt", "source", "alias", "unalias", "eval", "exec", "trap", "test",
                "wait", "kill", "jobs", "fg", "bg", "let", "getopts", "hash", "type", "ulimit", "umask", "command",
                "builtin", "enable", "help", "history", "pushd", "popd", "dirs", "mapfile", "readarray", "break",
                "continue", ":"],
    "constant": ["true", "false"]
  },
  "regions": [
    {"scope": "string", "begin": "\\$'", "end": "'", "escape": "\\"},
    {"scope": "string", "begin": "\"",   "end": "\"", "escape": "\\"},
    {"scope": "string", "begin": "'",    "end": "'"}
  ],
  "rules": [
    {"scope": "comment",  "pattern": "(?<![^\\s;|&(])#.*"},
    {"scope": "variable", "pattern": "\\$(?:\\{[^}\\n]*\\}|[A-Za-z_]\\w*|[0-9@*#?$!\\-])"},
    {"scope": "function", "pattern": "^\\s*[A-Za-z_][\\w:.\\-]*(?=\\s*\\(\\))"},
    {"scope": "function", "pattern": "(?<=function )[A-Za-z_][\\w:.\\-]*"},
    {"scope": "variable", "pattern": "(?<![\\w\\-])[A-Za-z_]\\w*(?==[^=])"},
    {"scope": "number",   "pattern": "(?<![\\w.\\-])\\d+(?![\\w.])"}
  ]
})JSON";

static const char* const kBatch = R"JSON({
  "name": "Batch",
  "revision": 1,
  "builtin": true,
  "extensions": ["bat", "cmd"],
  "caseSensitive": false,
  "words": {
    "keyword": ["if", "else", "for", "in", "do", "goto", "call", "exit", "not", "exist", "defined", "errorlevel",
                "equ", "neq", "lss", "leq", "gtr", "geq"],
    "builtin": ["echo", "set", "setlocal", "endlocal", "enabledelayedexpansion", "disabledelayedexpansion",
                "enableextensions", "cd", "chdir", "md", "mkdir", "rd", "rmdir", "del", "erase", "copy", "xcopy",
                "robocopy", "move", "ren", "rename", "type", "cls", "pause", "title", "color", "start", "shift",
                "pushd", "popd", "path", "prompt", "ver", "vol", "dir", "find", "findstr", "sort", "more", "assoc",
                "ftype", "taskkill", "tasklist", "timeout", "choice", "where", "mklink", "attrib"],
    "constant": ["on", "off", "nul"]
  },
  "regions": [
    {"scope": "string", "begin": "\"", "end": "\"", "multiline": false}
  ],
  "rules": [
    {"scope": "comment",  "pattern": "^\\s*@?\\s*(?:rem\\b.*|::.*)"},
    {"scope": "label",    "pattern": "^\\s*:(?!:)[^\\s:][^\\n]*"},
    {"scope": "label",    "pattern": "(?<=goto )[:\\w]+"},
    {"scope": "label",    "pattern": "(?<=call ):\\w+"},
    {"scope": "variable", "pattern": "%%~?[A-Za-z]|%~[A-Za-z$:]*\\d|%[\\d*]"},
    {"scope": "variable", "pattern": "%[^%\\s]+%"},
    {"scope": "variable", "pattern": "![^!\\s]+!"},
    {"scope": "number",   "pattern": "\\b\\d+\\b"}
  ]
})JSON";

static const char* const kHtml = R"JSON({
  "name": "HTML / XML",
  "revision": 1,
  "builtin": true,
  "extensions": ["html", "htm", "xhtml", "xml", "svg", "xsl", "xslt"],
  "regions": [
    {"scope": "comment", "begin": "<!--", "end": "-->"}
  ],
  "rules": [
    {"scope": "preprocessor", "pattern": "<![A-Za-z][^>]*>|<\\?[^>]*\\?>"},
    {"scope": "keyword",      "pattern": "</?[A-Za-z][\\w:.\\-]*"},
    {"scope": "keyword",      "pattern": "(?<=[\"'\\w])>|/>"},
    {"scope": "string",       "pattern": "(?:(?<==)|(?<==\\s))\"[^\"]*\""},
    {"scope": "string",       "pattern": "(?:(?<==)|(?<==\\s))'[^']*'"},
    {"scope": "type",         "pattern": "(?<![\\w\\-])[A-Za-z_:@][\\w:.\\-]*(?==)(?=[^<>]*>)"},
    {"scope": "escape",       "pattern": "&(?:#x?[0-9A-Fa-f]+|\\w+);"}
  ]
})JSON";

static const char* const kPython = R"JSON({
  "name": "Python",
  "revision": 1,
  "builtin": true,
  "extensions": ["py", "pyw", "pyi", "pyx", "gyp", "gypi"],
  "filenames": ["SConstruct", "SConscript", "Pipfile", "wscript"],
  "words": {
    "keyword": ["and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del", "elif", "else",
                "except", "finally", "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal", "not",
                "or", "pass", "raise", "return", "try", "while", "with", "yield"],
    "constant": ["True", "False", "None", "Ellipsis", "NotImplemented", "__debug__"],
    "builtin": ["print", "len", "range", "int", "str", "float", "list", "dict", "set", "tuple", "bool", "open", "input",
                "type", "isinstance", "issubclass", "super", "enumerate", "zip", "map", "filter", "sorted", "sum", "min",
                "max", "abs", "any", "all", "repr", "id", "hash", "iter", "next", "object", "bytes", "bytearray",
                "format", "getattr", "setattr", "hasattr", "delattr", "callable", "property", "staticmethod",
                "classmethod", "dir", "vars", "round", "pow", "divmod", "chr", "ord", "hex", "oct", "bin", "slice",
                "reversed", "frozenset", "complex", "globals", "locals", "exec", "eval", "compile", "memoryview"],
    "variable": ["self", "cls"]
  },
  "regions": [
    {"scope": "string", "begin": "(?<!\\w)(?:[rRbBfFuU]{1,2})?\"\"\"", "end": "\"\"\"", "escape": "\\"},
    {"scope": "string", "begin": "(?<!\\w)(?:[rRbBfFuU]{1,2})?'''",    "end": "'''",    "escape": "\\"},
    {"scope": "string", "begin": "(?<!\\w)(?:[rRbBfFuU]{1,2})?\"",      "end": "\"",     "escape": "\\", "multiline": false},
    {"scope": "string", "begin": "(?<!\\w)(?:[rRbBfFuU]{1,2})?'",       "end": "'",      "escape": "\\", "multiline": false}
  ],
  "rules": [
    {"scope": "comment",      "pattern": "#.*"},
    {"scope": "preprocessor", "pattern": "^\\s*@[\\w.]+"},
    {"scope": "number",       "pattern": "\\b(?:0[xX][0-9a-fA-F_]+|0[oO][0-7_]+|0[bB][01_]+|(?:\\d[\\d_]*\\.?[\\d_]*|\\.\\d+)(?:[eE][+-]?\\d+)?[jJ]?)\\b"},
    {"scope": "function",     "pattern": "(?<=def )[A-Za-z_]\\w*"},
    {"scope": "type",         "pattern": "(?<=class )[A-Za-z_]\\w*"},
    {"scope": "builtin",      "pattern": "\\b__\\w+__\\b"},
    {"scope": "function",     "pattern": "\\b[A-Za-z_]\\w*(?=\\s*\\()"},
    {"scope": "constant",     "pattern": "\\b[A-Z][A-Z0-9_]{2,}\\b"},
    {"scope": "type",         "pattern": "\\b[A-Z][A-Za-z0-9]*\\b"}
  ]
})JSON";

static const char* const kCss = R"JSON({
  "name": "CSS",
  "revision": 1,
  "builtin": true,
  "extensions": ["css"],
  "words": {
    "constant": ["none", "auto", "inherit", "initial", "unset", "revert", "transparent", "currentColor"]
  },
  "regions": [
    {"scope": "comment", "begin": "/\\*", "end": "\\*/"},
    {"scope": "string",  "begin": "\"",   "end": "\"", "escape": "\\", "multiline": false},
    {"scope": "string",  "begin": "'",    "end": "'",  "escape": "\\", "multiline": false}
  ],
  "rules": [
    {"scope": "preprocessor", "pattern": "@[A-Za-z-]+"},
    {"scope": "keyword",      "pattern": "!\\s*important"},
    {"scope": "variable",     "pattern": "(?<![\\w.#:-])-{0,2}[A-Za-z][\\w-]*(?=\\s*:)(?=[^{}]*[;}])"},
    {"scope": "builtin",      "pattern": "::?[A-Za-z-]+(?:\\([^)]*\\))?(?=[^;{}]*\\{)"},
    {"scope": "number",       "pattern": "#[0-9a-fA-F]{3,8}\\b"},
    {"scope": "number",       "pattern": "(?<![\\w#.-])-?(?:\\d+\\.?\\d*|\\.\\d+)(?:[a-zA-Z]+|%)?"},
    {"scope": "function",     "pattern": "[A-Za-z-]+(?=\\()"},
    {"scope": "type",         "pattern": "(?<![\\w)])\\.[A-Za-z_][\\w-]*"},
    {"scope": "constant",     "pattern": "#[A-Za-z_][\\w-]*"}
  ]
})JSON";

static const char* const kJs = R"JSON({
  "name": "JavaScript / TypeScript",
  "revision": 1,
  "builtin": true,
  "extensions": ["js", "mjs", "cjs", "jsx", "ts", "tsx"],
  "words": {
    "keyword": ["break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do", "else",
                "export", "extends", "finally", "for", "function", "if", "import", "in", "instanceof", "let", "new",
                "return", "super", "switch", "this", "throw", "try", "typeof", "var", "void", "while", "with", "yield",
                "async", "await", "static", "of", "from", "as", "interface", "enum", "implements", "namespace",
                "declare", "readonly", "abstract", "keyof", "satisfies", "private", "protected", "public"],
    "constant": ["true", "false", "null", "undefined", "NaN", "Infinity"],
    "builtin": ["console", "window", "document", "JSON", "Math", "Object", "Array", "String", "Number", "Boolean", "Date",
                "Promise", "Map", "Set", "WeakMap", "WeakSet", "RegExp", "Error", "Symbol", "require", "module",
                "exports", "process", "globalThis", "parseInt", "parseFloat", "setTimeout", "setInterval",
                "clearTimeout", "clearInterval", "fetch", "Buffer", "localStorage", "sessionStorage"],
    "type": ["string", "number", "boolean", "any", "unknown", "never", "object", "bigint", "symbol"]
  },
  "regions": [
    {"scope": "comment", "begin": "/\\*", "end": "\\*/"},
    {"scope": "string",  "begin": "`",    "end": "`",  "escape": "\\"},
    {"scope": "string",  "begin": "\"",   "end": "\"", "escape": "\\", "multiline": false},
    {"scope": "string",  "begin": "'",    "end": "'",  "escape": "\\", "multiline": false}
  ],
  "rules": [
    {"scope": "comment",  "pattern": "//.*"},
    {"scope": "string",   "pattern": "(?:(?<=[=(,:\\[!&|?{};]\\s)|(?<=[=(,:\\[!&|?{};])|(?<=\\breturn\\s))/(?![*/\\s])(?:\\\\.|\\[(?:\\\\.|[^\\]\\\\\\n])*\\]|[^/\\\\\\n\\[])+/[gimsuyd]*"},
    {"scope": "number",   "pattern": "\\b(?:0[xX][0-9a-fA-F_]+|0[bB][01_]+|0[oO][0-7_]+|(?:\\d[\\d_]*\\.?[\\d_]*|\\.\\d+)(?:[eE][+-]?\\d+)?n?)\\b"},
    {"scope": "function", "pattern": "(?<=function )[A-Za-z_$][\\w$]*"},
    {"scope": "type",     "pattern": "(?<=class )[A-Za-z_$][\\w$]*"},
    {"scope": "function", "pattern": "[A-Za-z_$][\\w$]*(?=\\s*\\()"},
    {"scope": "constant", "pattern": "\\b[A-Z][A-Z0-9_]{2,}\\b"},
    {"scope": "type",     "pattern": "\\b[A-Z][A-Za-z0-9_$]*\\b"}
  ]
})JSON";

extern const char* const kStockSyntaxFiles[];
extern const char* const kStockSyntaxJson[];
const char* const kStockSyntaxFiles[] = {"c.json", "cpp.json", "makefile.json", "ld.json", "rust.json",
                                                "bash.json", "batch.json", "html.json", "python.json", "css.json", "js.json", nullptr};
const char* const kStockSyntaxJson[]  = {kC, kCpp, kMakefile, kLd, kRust, kBash, kBatch, kHtml, kPython, kCss, kJs, nullptr};

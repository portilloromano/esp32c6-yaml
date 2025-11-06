# Code Generation Guardrails (Codex / Copilot)

## Purpose
This document defines strict rules for automatic code generation in the `yml2esp` project.
Its goal is to ensure deterministic, maintainable, and safe code when using models such as GitHub Copilot, Codex, or ChatGPT.

## Hard Rules (Non-Negotiable)

1. Output only code, no explanations, no comments, in English.
2. Firmware code must be in C/C++17.
3. Modify only the files specified in the prompt.
4. Use ESP-IDF 5.x and ESP-Matter. No new external dependencies.
5. MAC-derived commissioning credentials must use the deterministic algorithm described here.
6. Register the commissioning provider before `chip::Server::GetInstance().Init()`.
7. No logs, no prints, no allocations outside ESP-Matter patterns.

## Credential Derivation Algorithm
```
mac48 = packed 48-bit MAC
pin_raw = (mac48 ^ (mac48 >> 12) ^ 0x5A5A5A5A5A5A) % 99999999
setup_passcode = 10000000 + (pin_raw % 89999999)
discriminator = (mac48 ^ (mac48 >> 24)) & 0x0FFF
```

If MAC read fails:
```
setup_passcode = 20202021
discriminator = 3840
```

## Prompt Template
```
Follow docs/codegen_guardrails.md strictly.
Task: <short description>
Files: <file paths>
Output: only code.
```

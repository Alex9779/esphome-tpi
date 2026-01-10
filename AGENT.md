# AGENT.md - ESPHome TPI Component
## Development Guidelines

- Do NOT create example.yaml or other example configuration files
- Keep component documentation in README.md for usage instructions
- Follow ESPHome coding conventions from https://github.com/esphome/esphome/blob/dev/.ai/instructions.md
  - Use `protected` member variables with trailing underscore: `member_var_`
  - Prefix all member access with `this->`
  - Use `esphome::namespace` syntax (not `namespace esphome { namespace x {`)
  - 2-space indentation
  - 120 character line limit
  - Avoid STL containers when possible on embedded systems
  - Use proper component patterns (schema, to_code, etc.)
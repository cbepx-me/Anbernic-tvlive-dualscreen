# Contributing to TVLive DualScreen

We welcome contributions! Here’s how you can help.

## Reporting Issues

- Check the [issue tracker](https://github.com/yourusername/tvlive-dualscreen/issues) for duplicates.
- Provide detailed steps to reproduce, device model, firmware version, and relevant logs.

## Pull Requests

1. Fork the repo and create your branch from `main`.
2. If you add new features, please include tests if possible.
3. Ensure your code passes linting (PEP8) and has no obvious bugs.
4. Update the README and documentation if needed.
5. Open a PR with a clear description of what you changed and why.

## Code Style

- Follow PEP8.
- Use descriptive variable names.
- Add comments for non‑obvious logic.

## Development Setup

Install development dependencies:
```bash
pip install -r requirements-dev.txt  # (if exists)
```

Run the program with debug logging:
```bash
python3 tv.py 2>&1 | tee debug.log
```

## License
By contributing, you agree that your contributions will be licensed under the same MIT License as the project.

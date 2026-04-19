# Contributing to Home Indeed

Thank you for your interest in improving Home Indeed! This project aims to provide professional-grade Bible and Lyric overlays for churches using OBS Studio.

## 🤝 Code of Conduct
We are committed to a welcoming and inclusive environment. Please be respectful to all contributors.

## 🛠️ Development Setup
1. **Fork the Repository**: Create your own copy of the repo on GitHub.
2. **Clone Locally**: 
   ```bash
   git clone --recursive https://github.com/YOUR_USERNAME/Home-Indeed.git
   ```
3. **Install Dependencies**:
   *   **CMake** (3.24+)
   *   **Qt6** (Widgets, Network, Gui)
   *   **Visual Studio 2022** (Windows) or **Xcode** (macOS)

## 📐 Coding Standards
*   **Aesthetics First**: Any UI changes must feel premium and smooth.
*   **Thread Safety**: Always use `std::atomic` for cross-thread flags.
*   **Documentation**: Use Doxygen-style comments for all public headers.
*   **Consistency**: Follow the existing indentation and naming conventions.

## 🚀 Pull Request Process
1. Use the `develop` branch for all feature work.
2. Ensure your code compiles on at most as many platforms as possible (Windows/macOS/Linux).
3. Update the `README.md` if you add new features.
4. Your PR will be automatically built by GitHub Actions to verify stability.

## 📝 License
By contributing, you agree that your contributions will be licensed under the project's default license.

# Windows CI template

`windows.yml` is a template for building and exercising the application on a
Windows GitHub Actions runner. It is kept here rather than in `.github/workflows/`,
so it does not run until a maintainer reviews it and places it in that directory.
Creating workflow files may require additional repository permissions.

Review the branch trigger, release permissions and step results before enabling it.
Some smoke-test steps are marked `continue-on-error`; inspect their outcomes rather
than treating a published artifact as proof that every test passed.

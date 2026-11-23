load("@python_3_12//:defs.bzl", "interpreter")
load("@rules_python//python:pip.bzl", "pip_parse")

def install_python():
    pip_parse(
        name = "pip_deps",
        python_interpreter_target = interpreter,
        requirements_lock = "@srp_platform//third_party/python:requirements.txt",
    )

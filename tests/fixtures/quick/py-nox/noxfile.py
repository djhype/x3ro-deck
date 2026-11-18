import nox


@nox.session
def tests(session):
    session.run("pytest")


@nox.session(python=["3.11", "3.12"], name="type-check")
def typing(session):
    session.run("mypy")


@nox.session(reuse_venv=True)
@nox.parametrize("django", ["4.2", "5.0"])
def django(session, django):
    pass


def helper():
    pass

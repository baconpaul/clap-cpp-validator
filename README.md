# EXPERIMENTAL

This is an experiment in porting the rust based [clap validator](https://github.com/free-audio/clap-validator)
to C++ using Anthropic Claude. it would of course be much harder without that validator existing in the first place.
In addition to the 5 extensions tested by clap validator, this one tries to test a bunch more. See the docs directory.

But honstely I haven't even reviewed it yet! And a team has shown up to start maintaining the rust one again so
I'll probalby just leave this as fun for me for a bit.

See [docs/validator-design.md](docs/validator-design.md) for how the validator is put together
and what each check does. More files in the docs/ directory

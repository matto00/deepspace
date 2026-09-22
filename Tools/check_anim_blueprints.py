#!/usr/bin/env python3
"""
Checks Animation Blueprints hold wiring, not logic.

    python3 Tools/check_anim_blueprints.py

The project's rule is that all gameplay logic lives in C++ (ADR 0002). An
Animation Blueprint is a node graph in a binary asset, which is exactly where
logic goes to become invisible. ABP_DeepSpaceBody is allowed to *wire* values
C++ computed -- read a variable, play a blend space or a sequence, choose a
pose by an enum, output it -- and nothing else.

This is an allowlist, not a denylist: a node type nobody anticipated fails
until someone decides it belongs here. No editor needed: node class names are
stored as plain text in the asset, which is read as bytes. Exits non-zero on
any violation, so it can gate a build.
"""

import glob
import os
import re
import sys

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Every Animation Blueprint the project owns. The template's are Epic's, and
# full of state machines; they are not ours to police.
ANIM_BLUEPRINTS = ["Content/Characters/DeepSpace/ABP_*.uasset"]

ALLOWED = {
    "AnimGraphNode_Root",                 # the output pose
    "AnimGraphNode_BlendSpacePlayer",
    "AnimGraphNode_SequencePlayer",
    "AnimGraphNode_BlendListByEnum",      # "Blend Poses by EPosture"
    "AnimGraphNode_BlendListByBool",
    "AnimGraphNode_BlendListByInt",
    "K2Node_Event",                       # default event stubs every ABP gets
    "K2Node_VariableGet",                 # reading a value C++ computed
}

NODE = re.compile(rb"(?:AnimGraphNode|K2Node)_[A-Za-z0-9]+(?:_[A-Za-z0-9]+)*")


def node_tokens(path):
    with open(path, "rb") as f:
        return {m.decode() for m in NODE.findall(f.read())}


def allowed(token):
    # Tokens are class names, or instance and pin names derived from them
    # ("K2Node_Event_0", "AnimGraphNode_Root_0").
    return any(token == a or token.startswith(a + "_") for a in ALLOWED)


def main():
    files = sorted(f for pattern in ANIM_BLUEPRINTS
                   for f in glob.glob(os.path.join(PROJECT, pattern)))
    if not files:
        print("FAIL: no Animation Blueprints found to check (%s)" % ", ".join(ANIM_BLUEPRINTS))
        return 1
    failed = False
    for path in files:
        bad = sorted(t for t in node_tokens(path) if not allowed(t))
        name = os.path.relpath(path, PROJECT)
        if bad:
            failed = True
            print("FAIL %s contains logic, not wiring:" % name)
            for t in bad:
                print("  - " + t)
        else:
            print("ok   %s" % name)
    if failed:
        print("\nAnimation Blueprints may only wire values computed in C++ (ADR 0002).")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

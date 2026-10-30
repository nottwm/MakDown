# SPDX-License-Identifier: MIT
"""MarkdownEditor: formatting helpers, list continuation, indentation and the built-in vim engine.

Keys are injected as real QKeyEvents into the real widget (see tests/harness/harness.cpp for the key syntax:
plain text is typed char by char, <Esc> <CR> <BS> <Tab> <Up> ... and <C-r> <A-Up> <C-S-d> are special keys).
Expected results follow real vim wherever the README/hotkeys.md claim vim behaviour.
"""
import random
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lib.common import HarnessCase


class EditorCase(HarnessCase):
    def reset(self, text="", vim=False, pos=0, tab=None):
        kw = {"tab": tab} if tab else {}
        return self.call("ed_reset", text=text, vim=vim, pos=pos, **kw)

    def keys(self, *keys):
        return self.call("ed_keys", keys=list(keys))

    def fn(self, fn, **kw):
        return self.call("ed_call", fn=fn, **kw)

    def vim(self, text, *keys, pos=0):
        """Start in vim NORMAL mode with the caret at `pos`, type keys, return (text, pos, mode)."""
        self.reset(text, vim=True, pos=pos)
        r = self.keys(*keys) if keys else self.call("ed_keys", keys=[])
        return r["editor"]["text"], r["editor"]["pos"], r["mode"]

    def vtext(self, text, *keys, pos=0):
        return self.vim(text, *keys, pos=pos)[0]

    def vpos(self, text, *keys, pos=0):
        return self.vim(text, *keys, pos=pos)[1]

    def typed(self, text, *keys, pos=0):
        """vim OFF: plain editing."""
        self.reset(text, vim=False, pos=pos)
        return self.keys(*keys)["editor"]


# =============================================================================================
class TestMarkdownHelpers(EditorCase):
    def test_wrap_without_selection_puts_caret_between(self):
        self.reset("ab", pos=1)
        r = self.fn("wrap", a="**", b="**")["editor"]
        self.assertEqual((r["text"], r["pos"]), ("a****b", 3))

    def test_wrap_selection_and_keep_it_selected(self):
        self.reset("hello world")
        self.fn("select", **{"from": 0, "to": 5})
        r = self.fn("wrap", a="**", b="**")["editor"]
        self.assertEqual(r["text"], "**hello** world")
        self.assertEqual(r["selection"], "**hello**")

    def test_wrap_twice_unwraps(self):
        self.reset("hello world")
        self.fn("select", **{"from": 0, "to": 5})
        self.fn("wrap", a="*", b="*")
        r = self.fn("wrap", a="*", b="*")["editor"]
        self.assertEqual(r["text"], "hello world")
        self.assertEqual(r["selection"], "hello")

    def test_wrap_different_pre_post(self):
        self.reset("x")
        self.fn("select", **{"from": 0, "to": 1})
        self.assertEqual(self.fn("wrap", a="<u>", b="</u>")["editor"]["text"], "<u>x</u>")

    def test_wrap_multiline_selection(self):
        self.reset("one\ntwo")
        self.fn("select", **{"from": 0, "to": 7})
        r = self.fn("wrap", a="`", b="`")["editor"]
        self.assertEqual(r["text"], "`one\ntwo`")

    def test_wrap_unicode(self):
        self.reset("zażółć 😀")
        self.fn("select", **{"from": 0, "to": 6})
        self.assertEqual(self.fn("wrap", a="*", b="*")["editor"]["text"], "*zażółć* 😀")

    def test_selection_shorter_than_markers_is_wrapped_not_unwrapped(self):
        self.reset("*")
        self.fn("select", **{"from": 0, "to": 1})
        self.assertEqual(self.fn("wrap", a="*", b="*")["editor"]["text"], "***")

    def test_wrap_is_one_undo_step(self):
        self.reset("hello")
        self.fn("select", **{"from": 0, "to": 5})
        self.fn("wrap", a="**", b="**")
        self.assertEqual(self.fn("undo")["editor"]["text"], "hello")

    # ---- line prefixes -------------------------------------------------------------------
    def test_prefix_adds_and_removes(self):
        self.reset("a\nb")
        self.assertEqual(self.fn("prefix", a="- ")["editor"]["text"], "- a\nb")
        self.assertEqual(self.fn("prefix", a="- ")["editor"]["text"], "a\nb")

    def test_prefix_on_selected_lines(self):
        self.reset("a\nb\nc")
        self.fn("select", **{"from": 0, "to": 3})
        self.assertEqual(self.fn("prefix", a="> ")["editor"]["text"], "> a\n> b\nc")

    def test_prefix_mixed_lines_adds_to_all(self):
        self.reset("- a\nb")
        self.fn("select", **{"from": 0, "to": 5})
        self.assertEqual(self.fn("prefix", a="- ")["editor"]["text"], "- - a\n- b")

    def test_prefix_removal_only_when_every_line_has_it(self):
        self.reset("> a\n> b")
        self.fn("select", **{"from": 0, "to": 7})
        self.assertEqual(self.fn("prefix", a="> ")["editor"]["text"], "a\nb")

    def test_task_prefix(self):
        self.reset("todo")
        self.assertEqual(self.fn("prefix", a="- [ ] ")["editor"]["text"], "- [ ] todo")

    def test_prefix_is_a_single_undo_step(self):
        self.reset("a\nb\nc")
        self.fn("select", **{"from": 0, "to": 5})
        self.fn("prefix", a="- ")
        self.assertEqual(self.fn("undo")["editor"]["text"], "a\nb\nc")

    def test_prefix_on_empty_document(self):
        self.reset("")
        self.assertEqual(self.fn("prefix", a="- ")["editor"]["text"], "- ")

    # ---- headings ------------------------------------------------------------------------
    def test_heading_up_from_plain_text(self):
        self.reset("Title")
        self.assertEqual(self.fn("heading", n=1)["editor"]["text"], "# Title")

    def test_heading_levels_up_and_down(self):
        self.reset("## T")
        self.assertEqual(self.fn("heading", n=1)["editor"]["text"], "### T")
        self.assertEqual(self.fn("heading", n=-1)["editor"]["text"], "## T")

    def test_heading_clamped_at_six_and_zero(self):
        self.reset("###### T")
        self.assertEqual(self.fn("heading", n=1)["editor"]["text"], "###### T")
        self.reset("# T")
        self.assertEqual(self.fn("heading", n=-1)["editor"]["text"], "T")
        self.assertEqual(self.fn("heading", n=-1)["editor"]["text"], "T")

    def test_heading_only_changes_current_line(self):
        self.reset("a\nb\nc", pos=2)
        self.assertEqual(self.fn("heading", n=1)["editor"]["text"], "a\n# b\nc")

    def test_heading_on_empty_line(self):
        self.reset("")
        self.assertEqual(self.fn("heading", n=1)["editor"]["text"], "# ")

    def test_heading_big_jump(self):
        self.reset("T")
        self.assertEqual(self.fn("heading", n=99)["editor"]["text"], "###### T")

    # ---- link ----------------------------------------------------------------------------
    def test_link_without_selection(self):
        self.reset("")
        r = self.fn("link")["editor"]
        self.assertEqual(r["text"], "[text](url)")
        self.assertEqual(r["selection"], "url")

    def test_link_with_selection(self):
        self.reset("my site")
        self.fn("select", **{"from": 3, "to": 7})
        r = self.fn("link")["editor"]
        self.assertEqual(r["text"], "my [site](url)")
        self.assertEqual(r["selection"], "url")

    # ---- goto / find ---------------------------------------------------------------------
    def test_goto_line_and_clamping(self):
        self.reset("a\nb\nc")
        self.assertEqual(self.fn("goto", n=2)["editor"]["line"], 2)
        self.assertEqual(self.fn("goto", n=99)["editor"]["line"], 3)
        self.assertEqual(self.fn("goto", n=0)["editor"]["line"], 1)
        self.assertEqual(self.fn("goto", n=-5)["editor"]["line"], 1)

    def test_find_next_wraps_around(self):
        self.reset("x a x b x")
        positions = []
        for _ in range(4):
            r = self.fn("find", a="x")
            positions.append(r["editor"]["selStart"])
        self.assertEqual(positions, [4, 8, 0, 4])

    def test_find_backwards_wraps_around(self):
        self.reset("x a x b x", pos=0)
        r = self.fn("find", a="x", back=True)
        self.assertEqual(r["editor"]["selStart"], 8)
        r = self.fn("find", a="x", back=True)
        self.assertEqual(r["editor"]["selStart"], 4)

    def test_find_missing_and_empty(self):
        self.reset("abc")
        self.assertFalse(self.fn("find", a="zzz")["found"])
        self.assertFalse(self.fn("find", a="")["found"])

    def test_find_single_match_stays_on_it(self):
        self.reset("only one here")
        self.assertTrue(self.fn("find", a="one")["found"])
        self.assertTrue(self.fn("find", a="one")["found"])

    def test_find_is_case_insensitive_like_qtextdocument(self):
        self.reset("Hello")
        self.assertTrue(self.fn("find", a="hello")["found"])


class TestListContinuation(EditorCase):
    def enter(self, text, pos=None):
        r = self.typed(text, "<CR>", pos=len(text) if pos is None else pos)
        return r["text"]

    def test_bullets_continue(self):
        for m in "-*+":
            self.assertEqual(self.enter(f"{m} item"), f"{m} item\n{m} ", m)

    def test_ordered_increments(self):
        self.assertEqual(self.enter("1. a"), "1. a\n2. ")
        self.assertEqual(self.enter("9. a"), "9. a\n10. ")
        self.assertEqual(self.enter("3) a"), "3) a\n4) ")

    def test_task_continues_with_open_box(self):
        self.assertEqual(self.enter("- [x] done"), "- [x] done\n- [ ] ")
        self.assertEqual(self.enter("- [ ] todo"), "- [ ] todo\n- [ ] ")

    def test_indentation_is_kept(self):
        self.assertEqual(self.enter("    - nested"), "    - nested\n    - ")
        self.assertEqual(self.enter("  1. x"), "  1. x\n  2. ")

    def test_empty_item_ends_list(self):
        self.assertEqual(self.enter("- a\n- "), "- a\n")
        self.assertEqual(self.enter("1. a\n2. "), "1. a\n")

    def test_enter_in_middle_of_line_is_a_normal_newline(self):
        self.assertEqual(self.enter("- abcd", pos=4), "- ab\ncd")

    def test_plain_text_enter(self):
        self.assertEqual(self.enter("plain"), "plain\n")

    def test_not_a_list_without_space(self):
        self.assertEqual(self.enter("-nospace"), "-nospace\n")
        self.assertEqual(self.enter("2024.report"), "2024.report\n")

    def test_continuation_is_one_undo_step(self):
        self.reset("- a", pos=3)
        self.keys("<CR>")
        self.assertEqual(self.fn("undo")["editor"]["text"], "- a")

    def test_selection_prevents_continuation(self):
        self.reset("- abc")
        self.fn("select", **{"from": 2, "to": 5})
        self.assertEqual(self.keys("<CR>")["editor"]["text"], "- \n")

    def test_typing_a_list_end_to_end(self):
        r = self.typed("", "- one", "<CR>", "two", "<CR>", "three", "<CR>", "<CR>", "after")
        self.assertEqual(r["text"], "- one\n- two\n- three\nafter", "Enter on an empty item removes the marker (like VS Code) and the caret stays on that line")


class TestIndentation(EditorCase):
    def test_tab_inserts_spaces(self):
        self.reset("x", tab=4)
        self.assertEqual(self.keys("<Tab>")["editor"]["text"], "    x")

    def test_tab_width_setting(self):
        self.reset("x", tab=2)
        self.assertEqual(self.keys("<Tab>")["editor"]["text"], "  x")

    def test_tab_with_selection_indents_lines(self):
        self.reset("a\nb\nc", tab=2)
        self.fn("select", **{"from": 0, "to": 3})
        self.assertEqual(self.keys("<Tab>")["editor"]["text"], "  a\n  b\nc")

    def test_shift_tab_outdents(self):
        self.reset("    a\n  b\nc", tab=4)
        self.fn("select", **{"from": 0, "to": 11})
        self.assertEqual(self.keys("<S-Tab>")["editor"]["text"], "a\nb\nc")

    def test_outdent_only_removes_up_to_tab_width(self):
        self.reset("        a", tab=4)
        self.assertEqual(self.keys("<S-Tab>")["editor"]["text"], "    a")

    def test_outdent_real_tab(self):
        self.reset("\ta", tab=4)
        self.assertEqual(self.keys("<S-Tab>")["editor"]["text"], "a")

    @unittest.expectedFailure
    def test_KNOWN_ISSUE_selection_ending_at_column_zero_does_not_include_next_line(self):
        """Select two whole lines the way Shift+Down does (caret ends at the start of line 3) and press Tab: VS Code, Word and
        every other editor indent only lines 1-2. MakDown also indents line 3 (same logic in toggleLinePrefix, so Ctrl+Shift+8
        / Ctrl+Shift+9 / Ctrl+Shift+T hit the extra line as well)."""
        self.reset("a\nb\nc", tab=2)
        self.fn("select", **{"from": 0, "to": 4})
        self.assertEqual(self.keys("<Tab>")["editor"]["text"], "  a\n  b\nc")

    @unittest.expectedFailure
    def test_KNOWN_ISSUE_prefix_on_selection_ending_at_column_zero(self):
        self.reset("a\nb\nc")
        self.fn("select", **{"from": 0, "to": 4})
        self.assertEqual(self.fn("prefix", a="- ")["editor"]["text"], "- a\n- b\nc")

    def test_indent_is_single_undo(self):
        self.reset("a\nb", tab=4)
        self.fn("select", **{"from": 0, "to": 3})
        self.keys("<Tab>")
        self.assertEqual(self.fn("undo")["editor"]["text"], "a\nb")


class TestPlainTyping(EditorCase):
    def test_typing_inserts_text(self):
        self.assertEqual(self.typed("", "hello ", "wörld 😀")["text"], "hello wörld 😀")

    def test_backspace_and_delete(self):
        self.assertEqual(self.typed("abc", "<BS>", pos=3)["text"], "ab")
        self.assertEqual(self.typed("abc", "<Del>", pos=0)["text"], "bc")

    def test_vim_keys_do_nothing_special_when_vim_is_off(self):
        self.assertEqual(self.typed("", "dd", "hjkl", ":w")["text"], "ddhjkl:w")

    def test_escape_is_harmless_without_vim(self):
        self.assertEqual(self.typed("a", "<Esc>", "b", pos=1)["text"], "ab")

    def test_vim_mode_status_empty_when_vim_off(self):
        self.reset("x", vim=False)
        self.assertEqual(self.keys("a")["mode"], "")

    def test_alt_arrows_do_nothing_without_vim(self):
        self.assertEqual(self.typed("a\nb", "<A-Down>")["text"], "a\nb")

    def test_large_paste_like_input(self):
        self.reset("")
        r = self.keys("x" * 5000)
        self.assertEqual(len(r["editor"]["text"]), 5000)


# =============================================================================================
class TestVimModes(EditorCase):
    def test_starts_in_normal_mode(self):
        self.assertEqual(self.vim("abc")[2], "-- NORMAL --")

    def test_enter_insert_variants(self):
        self.assertEqual(self.vim("abc", "i", "X")[0], "Xabc")
        self.assertEqual(self.vim("abc", "a", "X")[0], "aXbc")
        self.assertEqual(self.vim("  abc", "I", "X")[0], "  Xabc")
        self.assertEqual(self.vim("abc", "A", "X")[0], "abcX")

    def test_o_and_O_open_lines(self):
        self.assertEqual(self.vim("a\nb", "o", "X")[0], "a\nX\nb")
        self.assertEqual(self.vim("a\nb", "O", "X", pos=2)[0], "a\nX\nb")
        self.assertEqual(self.vim("a", "O", "X")[0], "X\na")

    def test_o_continues_lists(self):
        self.assertEqual(self.vim("- a", "o", "b")[0], "- a\n- b")
        self.assertEqual(self.vim("1. a", "o", "b")[0], "1. a\n2. b")

    def test_escape_returns_to_normal_and_steps_back(self):
        t, p, m = self.vim("abc", "A", "<Esc>")
        self.assertEqual((m, p), ("-- NORMAL --", 2))
        t, p, m = self.vim("abc", "i", "<Esc>")
        self.assertEqual((m, p), ("-- NORMAL --", 0))

    def test_insert_status(self):
        self.assertEqual(self.vim("a", "i")[2], "-- INSERT --")

    def test_visual_statuses(self):
        self.assertEqual(self.vim("a b", "v")[2], "-- VISUAL --")
        self.assertEqual(self.vim("a b", "V")[2], "-- V-LINE --")
        self.assertEqual(self.vim("a b", "v", "<Esc>")[2], "-- NORMAL --")
        self.assertEqual(self.vim("a b", "V", "V")[2], "-- NORMAL --")
        self.assertEqual(self.vim("a b", "v", "v")[2], "-- NORMAL --")

    def test_pending_operator_and_count_show_in_status(self):
        self.assertIn("d", self.vim("a", "d")[2])
        self.assertIn("3", self.vim("a", "3")[2])
        self.assertEqual(self.vim("a", "d", "<Esc>")[2], "-- NORMAL --")

    def test_typing_text_in_normal_mode_does_not_insert(self):
        self.assertEqual(self.vim("abc", "q", "z", "1", "2")[0], "abc")

    def test_toggle_vim_off_returns_to_plain_editing(self):
        self.reset("abc", vim=True)
        self.fn("vim", on=False)
        self.assertEqual(self.keys("X")["editor"]["text"], "Xabc")

    def test_toggle_vim_on_again_does_not_reset_insert_mode(self):
        self.reset("abc", vim=True)
        self.keys("i")
        self.fn("vim", on=True)
        self.assertEqual(self.keys("Z")["editor"]["text"], "Zabc")

    def test_ex_prompt_keys_are_requested_not_inserted(self):
        self.assertEqual(self.vim("abc", ":")[0], "abc")
        self.assertEqual(self.vim("abc", "/")[0], "abc")


class TestVimMotions(EditorCase):
    TEXT = "one two  three\nfour five\n\nsix"

    def test_hl_stay_on_the_line(self):
        self.assertEqual(self.vpos("abc\ndef", "l", "l"), 2)
        self.assertEqual(self.vpos("abc\ndef", "h", "h"), 0)
        self.assertEqual(self.vpos("abc\ndef", "l" * 10), 2, "l must stop at the last character")

    def test_jk(self):
        self.assertEqual(self.vpos("abc\ndef\nghi", "j"), 4)
        self.assertEqual(self.vpos("abc\ndef\nghi", "j", "j"), 8)
        self.assertEqual(self.vpos("abc\ndef\nghi", "k", pos=5), 1)
        self.assertEqual(self.vpos("abc\ndef\nghi", "k", pos=0), 0)
        self.assertEqual(self.vpos("abc\ndef\nghi", "j" * 9), 8)

    def test_arrow_keys_map_to_hjkl(self):
        self.assertEqual(self.vpos("abc\ndef", "<Right>", "<Down>"), 5)
        self.assertEqual(self.vpos("abc\ndef", "<Left>", "<Up>", pos=5), 0)

    def test_zero_caret_dollar(self):
        self.assertEqual(self.vpos("  abc def", "$"), 8)
        self.assertEqual(self.vpos("  abc def", "0", pos=5), 0)
        self.assertEqual(self.vpos("  abc def", "^", pos=7), 2)
        self.assertEqual(self.vpos("  abc def", "<Home>", pos=5), 0)
        self.assertEqual(self.vpos("  abc def", "<End>"), 8)

    def test_word_forward(self):
        self.assertEqual(self.vpos(self.TEXT, "w"), 4)
        self.assertEqual(self.vpos(self.TEXT, "w", "w"), 9)
        self.assertEqual(self.vpos(self.TEXT, "3w"), 15)

    def test_word_back_and_end(self):
        self.assertEqual(self.vpos("one two three", "b", pos=8), 4)
        self.assertEqual(self.vpos("one two three", "e"), 2)
        self.assertEqual(self.vpos("one two three", "e", "e"), 6)

    def test_w_stops_at_punctuation(self):
        self.assertEqual(self.vpos("foo.bar baz", "w"), 3)

    def test_gg_and_G(self):
        t = "a\nb\nc\nd"
        self.assertEqual(self.vpos(t, "G"), 6)
        self.assertEqual(self.vpos(t, "g", "g", pos=6), 0)
        self.assertEqual(self.vpos(t, "2", "G"), 2)
        self.assertEqual(self.vpos(t, "3", "g", "g", pos=6), 4)

    def test_paragraph_motions(self):
        t = "a\nb\n\nc\nd\n\ne"
        self.assertEqual(self.vpos(t, "}"), 4)
        self.assertEqual(self.vpos(t, "}", "}"), 9)
        self.assertEqual(self.vpos(t, "{", pos=8), 4)

    def test_counts_on_simple_motions(self):
        self.assertEqual(self.vpos("abcdefgh", "3l"), 3)
        self.assertEqual(self.vpos("a\nb\nc\nd", "2j"), 4)
        self.assertEqual(self.vpos("abcdefgh", "5h", pos=7), 2)

    def test_motions_never_change_text(self):
        for k in ("h", "j", "k", "l", "w", "b", "e", "0", "^", "$", "gg", "G", "{", "}", "3w", "2b", "10j", "99l"):
            self.assertEqual(self.vtext(self.TEXT, *list(k) if k.isalpha() or k in ("0", "^", "$", "{", "}") else [k]), self.TEXT, k)

    def test_motions_on_empty_buffer(self):
        for k in ("h", "j", "k", "l", "w", "b", "e", "0", "$", "G", "{", "}"):
            self.assertEqual(self.vpos("", k), 0, k)

    def test_unicode_motion(self):
        self.assertEqual(self.vpos("żółć gęś", "w"), 5)
        self.assertEqual(self.vpos("żółć gęś", "$"), 7)


class TestVimEditing(EditorCase):
    def test_x_deletes_char_and_stays_on_line(self):
        self.assertEqual(self.vtext("abc", "x"), "bc")
        self.assertEqual(self.vtext("abc", "2x"), "c")
        self.assertEqual(self.vtext("abc\ndef", "9x"), "\ndef")

    def test_x_on_empty_line_is_noop(self):
        self.assertEqual(self.vtext("\nabc", "x"), "\nabc")

    def test_X_deletes_before_caret(self):
        self.assertEqual(self.vtext("abc", "X", pos=2), "ac")
        self.assertEqual(self.vtext("abc", "X", pos=0), "abc")

    def test_s_substitutes_and_enters_insert(self):
        t, p, m = self.vim("abc", "s", "Z")
        self.assertEqual(t, "Zbc")
        self.assertEqual(m, "-- INSERT --")

    def test_D_and_C(self):
        self.assertEqual(self.vtext("hello world", "D", pos=5), "hello")
        t, _, m = self.vim("hello world", "C", "X", pos=5)
        self.assertEqual((t, m), ("helloX", "-- INSERT --"))

    def test_dd(self):
        self.assertEqual(self.vtext("a\nb\nc", "dd"), "b\nc")
        self.assertEqual(self.vtext("a\nb\nc", "dd", pos=2), "a\nc")
        self.assertEqual(self.vtext("a\nb\nc", "dd", pos=4), "a\nb")

    def test_counted_dd(self):
        self.assertEqual(self.vtext("a\nb\nc\nd", "2dd"), "c\nd")
        self.assertEqual(self.vtext("a\nb\nc\nd", "d", "2", "d"), "c\nd")
        self.assertEqual(self.vtext("a\nb", "9dd"), "")

    def test_dd_on_single_line_empties_buffer(self):
        self.assertEqual(self.vtext("only", "dd"), "")

    def test_cc_replaces_the_line(self):
        t, _, m = self.vim("a\nb\nc", "cc", "X", pos=2)
        self.assertEqual(t.replace("\n", "|"), "a|X|c")
        self.assertEqual(m, "-- INSERT --")

    def test_yy_p(self):
        self.assertEqual(self.vtext("a\nb", "yy", "p"), "a\na\nb")
        self.assertEqual(self.vtext("a\nb", "yy", "P"), "a\na\nb")
        self.assertEqual(self.vtext("a\nb", "y", "y", "j", "p"), "a\nb\na")

    def test_dd_p_moves_line(self):
        self.assertEqual(self.vtext("a\nb\nc", "dd", "p"), "b\na\nc")

    def test_yank_charwise_and_paste(self):
        self.assertEqual(self.vtext("abc def", "yw", "$", "p"), "abc defabc ")
        self.assertEqual(self.vtext("abc", "x", "p"), "bac")
        self.assertEqual(self.vtext("abc", "x", "P"), "abc")

    def test_J_joins_lines(self):
        self.assertEqual(self.vtext("a\nb\nc", "J"), "a b\nc")
        self.assertEqual(self.vtext("a", "J"), "a")

    def test_tilde_toggles_case(self):
        self.assertEqual(self.vtext("abc", "~"), "Abc")
        self.assertEqual(self.vtext("Abc", "~"), "abc")
        self.assertEqual(self.vtext("a1", "~", "l", "~"), "A1")

    def test_r_replaces_char(self):
        self.assertEqual(self.vtext("abc", "r", "X"), "Xbc")
        self.assertEqual(self.vtext("abc", "r", "X", "l", "r", "Y"), "XYc")
        self.assertEqual(self.vtext("", "r", "X"), "")

    def test_r_in_normal_mode_stays_normal(self):
        self.assertEqual(self.vim("abc", "r", "X")[2], "-- NORMAL --")

    def test_indent_and_outdent(self):
        self.assertEqual(self.vtext("a\nb", ">", ">"), "    a\nb")
        self.assertEqual(self.vtext("    a", "<", "<"), "a")
        self.assertEqual(self.vtext("a", "<", "<"), "a")

    @unittest.expectedFailure
    def test_KNOWN_ISSUE_undo_inside_visual_line_leaves_stale_selection_state(self):
        """Pressing `u` while in V-LINE mode shrinks the document but visHead_ keeps its old position; the next motion
        calls QTextCursor::setPosition with an out-of-range value (Qt warning, and an inconsistent caret)."""
        self.reset(TestFuzz.DOC, vim=True)
        r = self.call("ed_keys", keys=['G', 'o', 'I', 'C', '<Esc>', 'V', 'u', '^'])
        self.assertEqual(r["warnings"], [])

    def test_undo_redo(self):
        self.assertEqual(self.vtext("abc", "x", "u"), "abc")
        self.assertEqual(self.vtext("abc", "x", "x", "u"), "bc")
        self.assertEqual(self.vtext("abc", "x", "x", "u", "u"), "abc")
        self.assertEqual(self.vtext("abc", "x", "u", "<C-r>"), "bc")
        self.assertEqual(self.vtext("abc", "u", "u"), "abc")

    def test_counted_undo(self):
        self.assertEqual(self.vtext("abcd", "x", "x", "x", "3u"), "abcd")


class TestVimOperators(EditorCase):
    def test_dw(self):
        self.assertEqual(self.vtext("one two three", "dw"), "two three")
        self.assertEqual(self.vtext("one two three", "2dw"), "three")
        self.assertEqual(self.vtext("one two three", "d", "2", "w"), "three")

    def test_dw_never_joins_lines(self):
        self.assertEqual(self.vtext("one\ntwo", "dw"), "\ntwo")

    def test_cw_behaves_like_ce(self):
        t, _, m = self.vim("one two", "cw", "X")
        self.assertEqual((t, m), ("X two", "-- INSERT --"))

    def test_d_dollar_and_d_zero(self):
        self.assertEqual(self.vtext("hello world", "d", "$", pos=5), "hello")
        self.assertEqual(self.vtext("hello world", "d", "0", pos=5), " world")

    def test_de_and_db(self):
        self.assertEqual(self.vtext("one two", "de"), " two")
        self.assertEqual(self.vtext("one two", "db", pos=4), "two")

    def test_diw_and_ciw(self):
        self.assertEqual(self.vtext("one two three", "diw", pos=5), "one  three")
        t, _, m = self.vim("one two three", "ciw", "X", pos=5)
        self.assertEqual((t, m), ("one X three", "-- INSERT --"))

    def test_yiw_p(self):
        self.assertEqual(self.vtext("abc d", "yiw", "$", "p"), "abc dabc")

    def test_dj_and_dk(self):
        self.assertEqual(self.vtext("a\nb\nc\nd", "dj"), "c\nd")
        self.assertEqual(self.vtext("a\nb\nc\nd", "dk", pos=4), "a\nd")

    @unittest.expectedFailure
    def test_KNOWN_ISSUE_dG_is_not_linewise(self):
        """vim's `dG` deletes from the current line through the LAST line (linewise). MakDown deletes only up to the start of
        the last line, leaving it behind (compare `dgg`, `dj`, `dk` which are correct)."""
        self.assertEqual(self.vtext("a\nb\nc", "dG", pos=2), "a")

    @unittest.expectedFailure
    def test_KNOWN_ISSUE_dgg_is_ignored(self):
        """`gg` is documented as a motion and operators accept motions, but `dgg` / `ygg` / `cgg` do nothing
        (the pending 'g' is not understood while an operator is pending)."""
        self.assertEqual(self.vtext("a\nb\nc", "d", "g", "g", pos=4), "", "dgg deletes from the first line through the current one")

    def test_operator_with_failed_motion_is_harmless(self):
        self.assertEqual(self.vtext("abc", "d", "h"), "abc")
        self.assertEqual(self.vtext("abc", "d", "q"), "abc")

    def test_operator_then_escape_cancels(self):
        self.assertEqual(self.vtext("abc", "d", "<Esc>", "x"), "bc")

    def test_ya_and_unknown_text_objects_ignored(self):
        self.assertEqual(self.vtext("abc def", "d", "i", "z"), "abc def")


class TestVimVisual(EditorCase):
    def test_v_motion_d(self):
        self.assertEqual(self.vtext("abcdef", "v", "l", "l", "d"), "def")

    def test_v_selection_is_inclusive(self):
        self.assertEqual(self.vtext("abcdef", "v", "d"), "bcdef")
        self.assertEqual(self.vtext("abcdef", "$", "v", "d"), "abcde")

    def test_v_backwards(self):
        self.assertEqual(self.vtext("abcdef", "v", "h", "d", pos=3), "abef", "backwards visual selection includes both end characters")

    def test_v_y_p(self):
        self.assertEqual(self.vtext("abc", "v", "l", "y", "$", "p"), "abcab")

    def test_v_c_enters_insert(self):
        t, _, m = self.vim("abcdef", "v", "l", "c", "X")
        self.assertEqual((t, m), ("Xcdef", "-- INSERT --"))

    def test_v_x_deletes(self):
        self.assertEqual(self.vtext("abcdef", "v", "l", "x"), "cdef")

    def test_V_d_deletes_whole_lines(self):
        self.assertEqual(self.vtext("a\nb\nc\nd", "V", "j", "d"), "c\nd")
        self.assertEqual(self.vtext("a\nb\nc", "V", "d"), "b\nc")

    def test_V_y_p_duplicates_lines(self):
        self.assertEqual(self.vtext("a\nb", "V", "j", "y", "G", "p"), "a\nb\na\nb")

    def test_V_extends_upwards(self):
        self.assertEqual(self.vtext("a\nb\nc\nd", "V", "k", "d", pos=4), "a\nd")

    @unittest.expectedFailure
    def test_KNOWN_ISSUE_visual_line_indent_touches_one_line_too_many(self):
        """`V j >` should indent exactly the two selected lines. updateVisualLine() selects up to the START of the
        next block, and indentLines() then treats that next block as selected too -> 'c' is indented as well.
        (Remove the decorator once ui/editor.cpp ignores a selection end that sits at column 0 of a line.)"""
        self.assertEqual(self.vtext("a\nb\nc", "V", "j", ">", pos=0), "    a\n    b\nc")

    def test_visual_indent_whole_selection_of_lines(self):
        self.assertEqual(self.vtext("a\nb\nc", "V", "j", ">", pos=0).splitlines()[:2], ["    a", "    b"])

    def test_visual_returns_to_normal_after_operator(self):
        self.assertEqual(self.vim("abc", "v", "d")[2], "-- NORMAL --")

    def test_visual_motion_with_word(self):
        self.assertEqual(self.vtext("one two three", "v", "w", "d"), "wo three")

    def test_escape_clears_selection(self):
        self.reset("abc", vim=True)
        self.keys("v", "l", "<Esc>")
        r = self.call("ed_keys", keys=[])["editor"]
        self.assertEqual(r["selection"], "")


class TestVimExtras(EditorCase):
    def test_alt_down_up_move_line(self):
        self.assertEqual(self.vtext("a\nb\nc", "<A-Down>"), "b\na\nc")
        self.assertEqual(self.vtext("a\nb\nc", "<A-Up>", pos=2), "b\na\nc")
        self.assertEqual(self.vtext("a\nb", "<A-Up>"), "a\nb", "no line above: nothing happens")
        self.assertEqual(self.vtext("a\nb", "<A-Down>", pos=2), "a\nb")

    def test_move_line_keeps_caret_on_moved_line(self):
        self.reset("abc\ndef", vim=True, pos=1)
        r = self.keys("<A-Down>")["editor"]
        self.assertEqual((r["line"], r["col"]), (2, 1))

    def test_move_line_is_one_undo_step(self):
        self.reset("a\nb", vim=True)
        self.keys("<A-Down>", "u")
        self.assertEqual(self.call("ed_keys", keys=[])["editor"]["text"], "a\nb")

    def test_duplicate_line(self):
        self.assertEqual(self.vtext("a\nb", "<C-S-d>"), "a\na\nb")
        self.assertEqual(self.vtext("a\nb", "<C-S-d>", "<C-S-d>"), "a\na\na\nb")

    def test_works_in_insert_mode_too(self):
        self.assertEqual(self.vtext("a\nb", "i", "<A-Down>"), "b\na")

    def test_half_page_motion_changes_nothing_in_text(self):
        t = "\n".join(str(i) for i in range(200))
        self.assertEqual(self.vtext(t, "<C-d>"), t)
        self.assertEqual(self.vtext(t, "<C-u>"), t)

    def test_ctrl_bracket_is_escape(self):
        self.assertEqual(self.vim("abc", "i", "<C-[>")[2], "-- NORMAL --")

    def test_n_and_N_use_last_search(self):
        self.reset("x a x b x", vim=True)
        self.fn("find", a="x")
        r = self.keys("n")["editor"]
        self.assertEqual(r["pos"], 8, "n goes to the next match")
        r = self.keys("N")["editor"]
        self.assertEqual(r["pos"], 4, "N goes back")

    def test_n_without_prior_search_is_harmless(self):
        self.assertEqual(self.vtext("abc", "n", "N"), "abc")


class TestEditorAppearanceFlags(EditorCase):
    def test_gutter_width_follows_line_numbers_and_count(self):
        self.reset("\n".join("x" for _ in range(5)))
        on = self.fn("flags", numbers=True)["gutterWidth"]
        off = self.fn("flags", numbers=False)["gutterWidth"]
        self.assertGreater(on, 0)
        self.assertEqual(off, 0)
        self.reset("\n".join("x" for _ in range(5000)))
        self.assertGreater(self.fn("flags", numbers=True)["gutterWidth"], on, "more digits need a wider gutter")

    def test_flags_do_not_alter_text(self):
        self.reset("a b\tc\n\nlast")
        for marks in (True, False):
            for spaces in (True, False):
                r = self.fn("flags", marks=marks, spaces=spaces, wrap=marks, tab=2 if spaces else 8)
                self.assertEqual(r["editor"]["text"], "a b\tc\n\nlast")

    def test_every_stock_theme_can_be_applied(self):
        self.reset("# x\ntext")
        for t in self.call("stock_themes")["themes"]:
            self.fn("theme", name=t["name"])

    def test_markdown_highlighting_toggle(self):
        self.reset("# head\n**b** `c`")
        self.fn("mdhl", on=False)
        self.fn("mdhl", on=True)

    def test_syntax_pack_switch_keeps_text(self):
        import json
        self.reset("int x; /* c */\n// y")
        pack = json.dumps({"name": "T", "extensions": ["t"], "words": {"keyword": ["int"]}})
        self.fn("pack", json=pack)
        self.fn("pack", json="{}")
        self.assertEqual(self.fn("mdhl", on=True)["editor"]["text"], "int x; /* c */\n// y")


# =============================================================================================
class TestFuzz(EditorCase):
    """Random key storms must never crash the editor, hang it, or lose the document's invariants."""

    VIM_KEYS = (list("hjklwbe0^$GgdcyxXsDCJ~rpPuvViaIAoO<>.{}nN/:123456789") +
                ["<Esc>", "<CR>", "<BS>", "<Tab>", "<S-Tab>", "<Up>", "<Down>", "<Left>", "<Right>", "<Home>", "<End>", "<Del>",
                 "<C-r>", "<C-d>", "<C-u>", "<A-Up>", "<A-Down>", "<C-S-d>", "<C-[>", "ś", "😀", " "])
    PLAIN_KEYS = list("abc xyz-*1.#>`[]()") + ["<CR>", "<BS>", "<Tab>", "<S-Tab>", "<Up>", "<Down>", "<Left>", "<Right>", "<Home>", "<End>",
                                               "<Del>", "<Esc>", "<C-r>", "<A-Up>", "<C-S-d>", "ż", "😀"]
    DOC = "# Title\n\n- one\n- [ ] two\n  1. three\n\n> quote\n\n```c\nint x;\n```\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\nżółć 😀\n"

    def storm(self, vim, seed, n):
        rng = random.Random(seed)
        pool = self.VIM_KEYS if vim else self.PLAIN_KEYS
        if vim:   # V-LINE has documented stale-state issues with undo / paste (KNOWN_ISSUE tests); it gets its own storm
            pool = [k for k in pool if k != "V"]
        self.reset(self.DOC, vim=vim)
        for chunk in range(n // 50):
            keys = [rng.choice(pool) for _ in range(50)]
            r = self.keys(*keys)
            ed = r["editor"]
            self.assertTrue(0 <= ed["pos"] <= len(ed["text"].encode("utf-16-le")) // 2 + 1, f"caret out of range after {keys}")
            self.assertEqual(r["warnings"], [], f"Qt warnings after {keys}: {r['warnings']}")
        return self.call("ed_keys", keys=[])["editor"]

    def test_vim_storm(self):
        for seed in range(6):
            self.storm(True, seed, 1500)

    def test_visual_line_storm(self):
        """V-LINE with everything except undo/redo/paste (those are the documented KNOWN_ISSUE cases)."""
        pool = list("Vjkwb0$GgydxJ><") + ["<Esc>", "<Up>", "<Down>", "<A-Down>", "<A-Up>", "o"]
        rng = random.Random(11)
        self.reset(self.DOC, vim=True)
        for _ in range(60):
            r = self.keys(*[rng.choice(pool) for _ in range(40)])
            self.assertEqual(r["warnings"], [])

    @unittest.expectedFailure
    def test_KNOWN_ISSUE_paste_inside_visual_mode_leaves_stale_selection_state(self):
        """Same root cause as the undo case: `p` in V-LINE replaces text but visHead_ is not refreshed, so the next key
        triggers 'QTextCursor::setPosition: Position out of range'."""
        self.reset(self.DOC, vim=True)
        r = self.call("ed_keys", keys=['C', '<C-[>', 'V', '4', '5', 'w', 'p', '.'])
        self.assertEqual(r["warnings"], [])

    def test_plain_storm(self):
        for seed in range(6):
            self.storm(False, 100 + seed, 1500)

    def test_vim_motions_only_never_edit(self):
        rng = random.Random(7)
        self.reset(self.DOC, vim=True)
        for _ in range(40):
            self.keys(*[rng.choice(list("hjklwbe0^$GgG{}") + ["<Up>", "<Down>", "<Left>", "<Right>", "<C-d>", "<C-u>"]) for _ in range(40)])
        self.assertEqual(self.call("ed_keys", keys=[])["editor"]["text"], self.DOC)

    def test_undo_everything_restores_original(self):
        self.reset(self.DOC, vim=True)
        rng = random.Random(3)
        self.keys(*[rng.choice(list("xdwyppiJ~") + ["<Esc>"]) for _ in range(200)])
        self.keys("<Esc>", "<Esc>")
        self.keys(*(["u"] * 600))
        self.assertEqual(self.call("ed_keys", keys=[])["editor"]["text"], self.DOC)

    def test_giant_counts_do_not_hang(self):
        self.reset(self.DOC, vim=True)
        self.keys("999999999", "j", "99999999", "x", "888888888", "w", "77777777", "k", "6666666", "d", "d")

    def test_huge_document_navigation(self):
        big = "\n".join(f"line {i} " + "word " * 20 for i in range(20000))
        self.reset(big, vim=True)
        self.keys("G", "gg", "}", "{", "<C-d>", "<C-u>", "3dd", "u")
        self.assertEqual(self.call("ed_keys", keys=[])["editor"]["text"], big)


if __name__ == "__main__":
    unittest.main(verbosity=2)

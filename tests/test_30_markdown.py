# SPDX-License-Identifier: MIT
"""Markdown -> rich-text renderer (core/markdown.cpp), exercised through the real compiled code.

Two views of every case are used:
  * html()  - the HTML string MakDown produces (structure assertions)
  * plain() - what Qt's QTextDocument makes of that HTML (what the user actually SEES)
Every README claim about the preview has at least one test here.
"""
import re
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from lib.common import HarnessCase


class MarkdownCase(HarnessCase):
    def html(self, md, **kw):
        return self.render(md, **kw)

    def has(self, md, needle, **kw):
        h = self.html(md, **kw)
        self.assertIn(needle, h, f"expected {needle!r} in render of {md!r}\n{h}")

    def hasnt(self, md, needle, **kw):
        h = self.html(md, **kw)
        self.assertNotIn(needle, h, f"did not expect {needle!r} in render of {md!r}\n{h}")


class TestBlocks(MarkdownCase):
    def test_empty_document(self):
        self.assertEqual(self.html(""), "<html><body></body></html>")

    def test_whitespace_only_document(self):
        self.assertEqual(self.html("   \n\n\t\n"), "<html><body></body></html>")

    def test_wrapped_in_html_body(self):
        h = self.html("x")
        self.assertTrue(h.startswith("<html><body>") and h.endswith("</body></html>"))

    def test_paragraph(self):
        self.has("Hello world", "<p>Hello world</p>")

    def test_soft_wrapped_lines_join(self):
        self.has("one\ntwo\nthree", "<p>one two three</p>")

    def test_blank_line_separates_paragraphs(self):
        h = self.html("a\n\nb")
        self.assertEqual(h.count("<p>"), 2)

    def test_hard_line_break_two_spaces(self):
        self.has("line one  \nline two", "line one<br>line two")

    def test_crlf_line_endings_behave_like_lf(self):
        self.assertEqual(self.html("# T\r\n\r\ntext\r\n- a\r\n- b\r\n"), self.html("# T\n\ntext\n- a\n- b\n"))

    def test_no_internal_tokens_leak(self):
        h = self.html("`code` **b** *i* [l](u) ![a](b.png)  \nbreak\n\n- item `x`\n")
        self.assertNotRegex(h, "[\x01\x02]", "private stash/BR tokens leaked into output")
        self.assertNotIn("BR", re.sub(r"<[^>]*>", "", h).replace("BREAK", ""))

    def test_horizontal_rules(self):
        for rule in ("---", "***", "___", "- - -", "* * *", "_ _ _", "-----------"):
            self.assertEqual(self.html(rule), "<html><body><hr></body></html>", f"rule {rule!r}")

    def test_two_dashes_are_not_a_rule(self):
        self.hasnt("--", "<hr>")

    def test_setext_style_is_not_special(self):
        # not claimed in the README; make sure it at least doesn't crash and keeps the text
        self.assertIn("Title", self.plain("Title\n====="))


class TestHeadings(MarkdownCase):
    def test_levels_one_to_six(self):
        for n in range(1, 7):
            self.has("#" * n + " Head", f"<h{n}>")
            self.has("#" * n + " Head", f"</h{n}>")

    def test_seven_hashes_is_not_a_heading(self):
        self.assertNotRegex(self.html("####### nope"), r"<h[1-6]>")

    def test_hash_without_space_is_not_a_heading(self):
        self.hasnt("#nospace", "<h1>")

    def test_trailing_hashes_are_removed(self):
        self.has("## Title ##", "</a>Title</h2>")

    def test_up_to_three_leading_spaces(self):
        self.has("   ### Indented", "<h3>")
        self.hasnt("    ### too much", "<h3>")

    def test_h1_h2_get_rule_when_theme_says_so(self):
        self.assertIn("<hr>", self.html("# a", theme="GitHub"))
        self.assertIn("<hr>", self.html("## a", theme="GitHub"))
        self.assertNotIn("<hr>", self.html("### a", theme="GitHub"))

    def test_no_rules_for_themes_without_heading_rules(self):
        self.assertNotIn("<hr>", self.html("# a", theme="Windows 11"))

    def test_heading_inline_formatting(self):
        self.has("# A **bold** `code`", "<b>bold</b>")

    def test_anchor_is_github_style(self):
        self.has("## My Section", '<a name="my-section"></a>')

    def test_anchor_strips_punctuation_and_markup(self):
        self.has("# Hello, **World**! (v2.0)", 'name="hello-world-v20"')
        self.has("# `code` and [link](http://x.y)", 'name="code-and-link"')

    def test_anchor_keeps_underscore_hyphen_and_unicode(self):
        self.has("# snake_case-name", 'name="snake_case-name"')
        self.has("# Żółć 日本語", 'name="żółć-日本語"')

    def test_duplicate_headings_get_numeric_suffix(self):
        h = self.html("# Intro\n# Intro\n# Intro")
        self.assertIn('name="intro"', h)
        self.assertIn('name="intro-1"', h)
        self.assertIn('name="intro-2"', h)

    def test_empty_heading_slug_falls_back(self):
        h = self.html("# !!!")
        self.assertRegex(h, r'name="section"')

    def test_reveal_markers_show_hashes(self):
        h = self.html("## T", reveal=True)
        self.assertIn("&#35;" if "&#35;" in h else "##", h)
        self.assertIn("color:", h)


class TestInline(MarkdownCase):
    def test_bold_italic_strike_code(self):
        self.has("**b**", "<b>b</b>")
        self.has("__b__", "<b>b</b>")
        self.has("*i*", "<i>i</i>")
        self.has("_i_", "<i>i</i>")
        self.has("~~s~~", "<s>s</s>")
        self.has("`c`", "&nbsp;c&nbsp;")

    def test_bold_and_italic_combined(self):
        h = self.html("***both***")
        self.assertIn("<b>", h)
        self.assertIn("<i>", h)

    def test_snake_case_is_not_italic(self):
        self.hasnt("some_snake_case_name", "<i>")

    def test_spaces_inside_stars_are_not_emphasis(self):
        self.hasnt("2 * 3 * 4", "<i>")
        self.hasnt("a ** b ** c", "<b>")

    def test_code_is_literal(self):
        h = self.html("`**not bold** <b>x</b> &amp;`")
        self.assertNotIn("<b>", h)
        self.assertIn("&lt;b&gt;x&lt;/b&gt;", h)
        self.assertIn("&amp;amp;", h)

    def test_html_is_escaped_in_text(self):
        h = self.html("<script>alert(1)</script> & <b>x</b>")
        self.assertNotIn("<script>", h)
        self.assertIn("&lt;script&gt;", h)
        self.assertIn("&amp;", h)

    def test_links(self):
        self.has("[text](https://example.com)", '<a href="https://example.com">text</a>')

    def test_link_with_title_ignored_gracefully(self):
        self.has('[t](https://e.com "Title")', '<a href="https://e.com">t</a>')

    def test_link_text_can_be_emphasised(self):
        self.has("[**bold link**](u)", "<b>bold link</b>")

    def test_link_target_not_emphasised(self):
        h = self.html("[x](http://a.b/*c*/d_e_f)")
        self.assertIn('href="http://a.b/*c*/d_e_f"', h)

    def test_autolink(self):
        self.has("<https://example.com/x>", '<a href="https://example.com/x">https://example.com/x</a>')

    def test_anchor_link_passes_through(self):
        self.has("[jump](#my-section)", 'href="#my-section"')

    def test_relative_md_link_with_anchor(self):
        self.has("[o](other.md#sec)", 'href="other.md#sec"')

    def test_remote_images_never_fetched(self):
        h = self.html("![logo](https://example.com/a.png)")
        self.assertNotIn("<img", h, "preview must not load remote images")
        self.assertIn("[image: logo]", h)

    def test_missing_local_image_placeholder(self):
        self.has("![nope](does/not/exist.png)", "[missing image: nope]")

    def test_existing_local_image(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "pic.png").write_bytes(b"\x89PNG\r\n\x1a\n")
            h = self.html("![pic](pic.png)", baseDir=d)
            self.assertIn("<img src=", h)
            self.assertIn(Path(d).name, h)
            self.assertIn('alt="pic"', h)

    def test_unicode_and_emoji_survive(self):
        p = self.plain("**Zażółć** gęślą jaźń 日本語 😀 ✓")
        self.assertIn("Zażółć gęślą jaźń 日本語 😀 ✓", p)

    def test_unclosed_markers_do_not_crash(self):
        for s in ("**unclosed", "*x", "`tick", "[a](", "[a]", "![", "~~", "***", "_", "[](x)", "![]()", "<", "&"):
            self.html(s)

    def test_html_entities_in_attributes_safe(self):
        h = self.html('[a"b](http://x/?a=1&b=2)')
        self.assertNotIn('"b"', h.replace('a"b', ""), "quote in link text must not break the attribute")


class TestLists(MarkdownCase):
    def test_bullets(self):
        for m in "-*+":
            h = self.html(f"{m} a\n{m} b")
            self.assertEqual(h.count("<li>"), 2, m)
            self.assertIn("<ul>", h)

    def test_ordered(self):
        h = self.html("1. a\n2. b\n3) c")
        self.assertIn("<ol>", h)
        self.assertEqual(h.count("<li>"), 3)

    def test_nested(self):
        h = self.html("- a\n  - b\n    - c\n- d")
        self.assertEqual(h.count("<ul>"), 3)
        self.assertEqual(h.count("</ul>"), 3)
        self.assertEqual(h.count("<li>"), 4)

    def test_nested_ordered_in_bullets(self):
        h = self.html("- a\n  1. x\n  2. y\n- b")
        self.assertIn("<ol>", h)
        self.assertIn("<ul>", h)
        self.assertEqual(h.count("<ol>"), h.count("</ol>"))

    def test_tab_indent_counts_as_four(self):
        self.assertEqual(self.html("- a\n\t- b").count("<ul>"), 2)

    def test_dedent_by_more_than_one_level(self):
        h = self.html("- a\n  - b\n    - c\n- d")
        self.assertEqual(h.count("<ul>"), h.count("</ul>"))

    def test_task_list(self):
        h = self.html("- [ ] open\n- [x] done\n- [X] done2")
        self.assertEqual(h.count("\u2610"), 1)
        self.assertEqual(h.count("\u2611"), 2)

    def test_continuation_line_joins_item(self):
        p = self.plain("- first line\n  wrapped part\n- second")
        self.assertIn("first line wrapped part", p)

    def test_blank_line_inside_loose_list_keeps_one_list(self):
        h = self.html("- a\n\n- b\n\n- c")
        self.assertEqual(h.count("<ul>"), 1)

    def test_list_interrupts_paragraph(self):
        h = self.html("para\n- item")
        self.assertIn("<p>para</p>", h)
        self.assertIn("<li>item", h)

    def test_hr_is_not_a_list_item(self):
        h = self.html("- a\n---\n- b")
        self.assertIn("<hr>", h)

    def test_inline_in_list_items(self):
        self.has("- **bold** and `code`", "<b>bold</b>")

    def test_reveal_shows_list_markers(self):
        h = self.html("- a", reveal=True)
        self.assertRegex(h, r"<li><span style=\"color:#[0-9a-f]{6}\">-&nbsp;</span>a")

    def test_no_reveal_has_no_ghost_spans(self):
        self.assertNotIn("<span", self.html("- a\n\n# h\n\n> q\n\n**b** *i*", reveal=False))

    def test_large_list_does_not_explode(self):
        md = "\n".join(f"- item {i}" for i in range(3000))
        self.assertEqual(self.html(md).count("<li>"), 3000)


class TestQuotesAndCode(MarkdownCase):
    def test_quote(self):
        h = self.html("> quoted")
        self.assertIn("quoted", h)
        self.assertIn("<table", h)

    def test_nested_quotes(self):
        h = self.html("> a\n> > b\n> > > c")
        self.assertGreaterEqual(h.count("<table"), 3)

    def test_quote_holds_blocks(self):
        h = self.html("> # Head\n> - item\n> ```\n> code\n> ```")
        self.assertIn("<h1>", h)
        self.assertIn("<li>", h)
        self.assertIn("<pre", h)

    def test_quote_without_space(self):
        self.has(">tight", "tight")

    def test_fenced_code_backticks_and_tildes(self):
        for f in ("```", "~~~"):
            h = self.html(f"{f}\nline1\nline2\n{f}")
            self.assertIn("<pre", h)
            self.assertIn("line1\nline2", h)

    def test_code_block_content_is_escaped_and_literal(self):
        h = self.html("```\n# not a heading\n- not a list\n**no** <b>\n```")
        self.assertNotIn("<h1>", h)
        self.assertNotIn("<li>", h)
        self.assertIn("&lt;b&gt;", h)
        self.assertNotIn("<b>no", h)

    def test_unterminated_fence_runs_to_end(self):
        h = self.html("```\ncode\nmore")
        self.assertIn("code\nmore", h)

    def test_tilde_fence_not_closed_by_backticks(self):
        h = self.html("~~~\na\n```\nb\n~~~\nafter")
        self.assertIn("a\n```\nb", h)
        self.assertIn("<p>after</p>", h)

    def test_language_label_when_theme_enables_it(self):
        self.assertIn(">cpp</div>", self.html("```cpp\nint x;\n```", theme="GitHub"))
        self.assertIn(">c++</div>", self.html("```c++\nint x;\n```", theme="GitHub"))
        self.assertIn(">c#</div>", self.html("```c#\nx\n```", theme="GitHub"))

    def test_no_label_without_language(self):
        self.assertNotIn("font-size", re.sub(r"<pre[^>]*>", "<pre>", self.html("```\nx\n```")).split("<pre>")[0].split("<td")[-1])

    def test_indented_fence(self):
        self.assertIn("<pre", self.html("  ```\n  x\n  ```"))

    def test_blank_lines_inside_code_preserved(self):
        self.assertIn("a\n\nb", self.html("```\na\n\nb\n```"))

    def test_code_block_uses_theme_colours(self):
        for theme, bg in (("GitHub", "#f6f8fa"), ("Nord", "#3b4252"), ("Catppuccin", "#11111b")):
            self.assertIn(f'bgcolor="{bg}"', self.html("```\nx\n```", theme=theme))


class TestTables(MarkdownCase):
    TABLE = "| A | B |\n|---|---|\n| 1 | 2 |\n| 3 | 4 |"

    def test_basic_table(self):
        h = self.html(self.TABLE)
        self.assertEqual(h.count("<th"), 2)
        self.assertEqual(h.count("<tr>"), 3)
        self.assertEqual(h.count("<td"), 4)

    def test_alignment(self):
        h = self.html("| L | C | R |\n|:--|:--:|--:|\n| a | b | c |")
        self.assertIn('align="left"', h)
        self.assertIn('align="center"', h)
        self.assertIn('align="right"', h)

    @unittest.expectedFailure
    def test_KNOWN_ISSUE_single_dash_separator_cells(self):
        """GitHub-flavoured Markdown allows ONE hyphen per separator cell (`|:-:|`, `|-|-|`).
        core/markdown.cpp's rxSep requires `-{2,}`, so such tables fall back to a plain paragraph.
        When this is fixed the test starts 'unexpectedly succeeding' - delete the decorator then."""
        h = self.html("| A | B |\n|:-:|-|\n| 1 | 2 |")
        self.assertIn("<table", h)

    def test_without_outer_pipes(self):
        h = self.html("A | B\n--|--\n1 | 2")
        self.assertEqual(h.count("<th"), 2)

    def test_short_rows_are_padded(self):
        h = self.html("| A | B | C |\n|--|--|--|\n| 1 |")
        self.assertEqual(h.count("<td"), 3)

    def test_long_rows_are_truncated_to_header(self):
        h = self.html("| A |\n|--|\n| 1 | 2 | 3 |")
        self.assertEqual(h.count("<td"), 1)

    def test_inline_markup_in_cells(self):
        h = self.html("| **a** | `b` |\n|--|--|\n| *c* | [d](x) |")
        for t in ("<b>a</b>", "<i>c</i>", '<a href="x">d</a>'):
            self.assertIn(t, h)

    def test_table_ends_at_blank_line(self):
        h = self.html(self.TABLE + "\n\nafter")
        self.assertIn("<p>after</p>", h)

    def test_pipe_text_without_separator_is_a_paragraph(self):
        h = self.html("a | b\nc | d")
        self.assertNotIn("<table", h)

    def test_rendered_table_has_expected_text(self):
        p = self.plain(self.TABLE)
        for cell in ("A", "B", "1", "2", "3", "4"):
            self.assertIn(cell, p)


class TestRevealMarkers(MarkdownCase):
    def test_emphasis_markers_are_visible_in_text_view(self):
        p = self.plain("**b** *i* ~~s~~ `c`", reveal=True)
        for m in ("**", "~~", "`"):
            self.assertIn(m, p)

    def test_quote_and_heading_markers(self):
        p = self.plain("# H\n\n> q", reveal=True)
        self.assertIn("# H", p)
        self.assertIn(">", p)

    def test_text_content_unchanged_by_reveal(self):
        md = "# Title\n\nSome **bold** text.\n\n- a\n- b"
        plain_off = self.plain(md)
        plain_on = self.plain(md, reveal=True)
        stripped = re.sub(r"[#*>\-`~_]|\s", "", plain_on)
        self.assertEqual(re.sub(r"[#*>\-`~_]|\s", "", plain_off), stripped)

    def test_ghost_marks_not_mistaken_for_markdown(self):
        # numeric entities: a literal "**" typed by the user must still render as bold, not twice
        h = self.html("**x**", reveal=True)
        self.assertEqual(h.count("<b>"), 1)


class TestQtActuallyParsesIt(MarkdownCase):
    """The HTML must survive QTextDocument (what QTextBrowser uses) without warnings or lost text."""

    DOC = ("# Title\n\nIntro with **bold**, *italic*, ~~strike~~, `code` and a [link](https://x.y).\n\n"
           "## List\n\n- one\n  - nested\n- [x] done\n- [ ] todo\n\n1. first\n2. second\n\n"
           "> quote\n> > deeper\n\n```py\nprint('hi')\n```\n\n| A | B |\n|:-|-:|\n| 1 | 2 |\n\n---\n\nEnd.")

    def test_every_theme_renders_the_kitchen_sink(self):
        themes = [t["name"] for t in self.call("stock_themes")["themes"]]
        self.assertGreaterEqual(len(themes), 15, "README promises 15 stock presets")
        for name in themes:
            for reveal in (False, True):
                p = self.plain(self.DOC, theme=name, reveal=reveal)
                for word in ("Title", "Intro", "italic", "strike", "link", "nested", "done", "todo", "second", "deeper",
                             "print('hi')", "End."):
                    self.assertIn(word, p, f"{word!r} missing from visible text with theme {name} reveal={reveal}")

    def test_no_magenta_fallback_colour_in_any_theme(self):
        """#ff00ff is Theme::color()'s 'loud' fallback for a missing key: it must never reach the output."""
        for t in self.call("stock_themes")["themes"]:
            for reveal in (False, True):
                self.assertNotIn("#ff00ff", self.html(self.DOC, theme=t["name"], reveal=reveal), t["name"])
            self.assertNotIn("#ff00ff", self.call("css", theme=t["name"])["css"], t["name"])

    def test_preview_css_is_well_formed(self):
        css = self.call("css", theme="GitHub")["css"]
        self.assertEqual(css.count("{"), css.count("}"))
        for sel in ("body", "a", "h1", "h2", "p", "li"):
            self.assertRegex(css, rf"(^|[ ,}}]){sel}[ ,{{]")


class TestRobustness(MarkdownCase):
    def test_pathological_inputs_finish_quickly(self):
        cases = {
            "many stars": "*" * 20000,
            "many backticks": "`" * 20000,
            "nested brackets": "[" * 3000 + "]" * 3000,
            "unclosed emphasis": "**a " * 3000,
            "long line": "word " * 100000,
            "deep quotes": "> " * 400 + "x",
            "deep list": "".join("  " * i + "- x\n" for i in range(150)),
            "many headings": "\n".join(f"# h{i % 5}" for i in range(5000)),
            "table 200x200": "|" + "a|" * 200 + "\n|" + "-|" * 200 + "\n" + ("|" + "x|" * 200 + "\n") * 200,
            "underscores": "_a" * 10000,
            "tilde storm": "~~a" * 5000,
        }
        for name, md in cases.items():
            t = time.time()
            self.html(md)
            self.assertLess(time.time() - t, 8, f"'{name}' took too long (possible catastrophic regex backtracking)")

    def test_binary_garbage_and_control_characters(self):
        junk = "".join(chr(i) for i in range(1, 32) if i not in (10, 13)) + "\x7f\u200b\ufeff\u202e\ufffd"
        self.html(junk)
        self.html(f"# {junk}\n- {junk}\n> {junk}\n```\n{junk}\n```\n| {junk} |\n|-|\n| {junk} |")

    def test_control_characters_do_not_mimic_internal_tokens(self):
        """\\x01/\\x02 are used internally as stash markers: user text containing them must not corrupt output."""
        h = self.html("a\x010\x02b `code`")
        self.assertIn("code", h)

    def test_very_long_document(self):
        md = ("# Heading\n\nParagraph with **bold** and `code`.\n\n- a\n- b\n\n" * 3000)
        self.assertGreater(len(self.html(md)), 100000)

    def test_idempotent_for_same_input(self):
        md = "# T\n\n**b** [l](#t)\n\n- a\n- [ ] b\n"
        self.assertEqual(self.html(md), self.html(md))
        self.assertEqual(self.html("# a\n# a"), self.html("# a\n# a"), "slug counters must reset between renders")


if __name__ == "__main__":
    unittest.main(verbosity=2)

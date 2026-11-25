//! HTML that cannot forget to escape.
//!
//! `Html` is markup that is known to be safe. There are exactly two ways to put something into it: a
//! `&'static str` literal that we wrote (`Html::lit`), and text from anywhere else (`Html::text`), which is
//! escaped. Tags are built with `Html::tag`, whose attribute values are escaped too. Nothing in `pages/`
//! builds markup by formatting a string, and `scripts/audit.sh` checks that.

use sha2::{Digest, Sha256};

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Html(String);

/// The five characters that can end text or an attribute value and start markup.
pub fn escape(s: &str) -> String {
    let mut out = String::with_capacity(s.len() + 8);
    for c in s.chars() {
        match c {
            '&' => out.push_str("&amp;"),
            '<' => out.push_str("&lt;"),
            '>' => out.push_str("&gt;"),
            '"' => out.push_str("&quot;"),
            '\'' => out.push_str("&#39;"),
            _ => out.push(c),
        }
    }
    out
}

impl Html {
    pub fn new() -> Html {
        Html(String::new())
    }

    /// Markup that we wrote ourselves. `'static` on purpose: a string built at run time cannot be passed.
    pub fn lit(s: &'static str) -> Html {
        Html(s.to_string())
    }

    /// Text from anywhere: escaped.
    pub fn text(s: &str) -> Html {
        Html(escape(s))
    }

    /// `<name attr="value" ...>inner</name>`; every attribute value is escaped, names are ours.
    pub fn tag(name: &'static str, attrs: &[(&'static str, &str)], inner: Html) -> Html {
        let mut out = String::from("<");
        out.push_str(name);
        for (k, v) in attrs {
            out.push(' ');
            out.push_str(k);
            out.push_str("=\"");
            out.push_str(&escape(v));
            out.push('"');
        }
        out.push('>');
        out.push_str(&inner.0);
        out.push_str("</");
        out.push_str(name);
        out.push('>');
        Html(out)
    }

    /// A tag with no content and no closing tag (`input`, `meta`, `br`).
    pub fn void(name: &'static str, attrs: &[(&'static str, &str)]) -> Html {
        let mut out = String::from("<");
        out.push_str(name);
        for (k, v) in attrs {
            out.push(' ');
            out.push_str(k);
            out.push_str("=\"");
            out.push_str(&escape(v));
            out.push('"');
        }
        out.push('>');
        Html(out)
    }

    /// An attribute that is only present (`required`, `selected`, `checked`): ours, `'static`.
    pub fn void_flags(name: &'static str, attrs: &[(&'static str, &str)], flags: &[&'static str]) -> Html {
        let mut out = String::from("<");
        out.push_str(name);
        for (k, v) in attrs {
            out.push(' ');
            out.push_str(k);
            out.push_str("=\"");
            out.push_str(&escape(v));
            out.push('"');
        }
        for f in flags {
            out.push(' ');
            out.push_str(f);
        }
        out.push('>');
        Html(out)
    }

    pub fn push(&mut self, other: &Html) {
        self.0.push_str(&other.0);
    }

    pub fn join(parts: impl IntoIterator<Item = Html>) -> Html {
        let mut out = Html::new();
        for p in parts {
            out.push(&p);
        }
        out
    }

    pub fn as_str(&self) -> &str {
        &self.0
    }

    pub fn into_string(self) -> String {
        self.0
    }

    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }
}

/// The only stylesheet of the phone pages, inlined in every page. Its SHA-256 is in the CSP, so no other
/// style can be injected either.
pub const CSS: &str = "\
:root{color-scheme:light dark;--bg:#f4ecd8;--fg:#2b2118;--card:#fffaf0;--line:#c9b68f;--gold:#9a6b00;--red:#8b1e1e;--ink:#fffaf0}\
@media (prefers-color-scheme:dark){:root{--bg:#1d1812;--fg:#f0e6cf;--card:#2a231a;--line:#5a4a30;--gold:#e0b04a;--red:#d9625a;--ink:#1d1812}}\
*{box-sizing:border-box}\
body{margin:0;background:var(--bg);color:var(--fg);font:18px/1.45 Georgia,'Times New Roman',serif}\
main{max-width:34rem;margin:0 auto;padding:1rem}\
h1{font-size:1.6rem;margin:.2rem 0 1rem}h2{font-size:1.2rem;margin:1.4rem 0 .5rem;color:var(--gold)}\
a{color:var(--gold)}\
.card{background:var(--card);border:1px solid var(--line);border-radius:.6rem;padding:.7rem .9rem;margin:.5rem 0}\
.done{opacity:.6;text-decoration:line-through}\
.muted{opacity:.75;font-size:.9rem}\
label{display:block;margin:.8rem 0 .2rem;font-weight:bold}\
input,textarea,select{width:100%;font:inherit;color:inherit;background:var(--card);border:1px solid var(--line);border-radius:.4rem;padding:.6rem}\
textarea{min-height:6rem}\
button,.button{display:inline-block;font:inherit;border:0;border-radius:.4rem;padding:.7rem 1.1rem;margin:.6rem .4rem .2rem 0;background:var(--gold);color:var(--ink);text-decoration:none;min-height:2.75rem}\
button.danger,.button.danger{background:var(--red);color:#fff}\
.error{border-left:.4rem solid var(--red);padding:.4rem .7rem;margin:.6rem 0;background:var(--card)}\
.row{display:flex;gap:.5rem;align-items:center;flex-wrap:wrap}\
nav a{margin-right:1rem}\
";

/// `Content-Security-Policy`: nothing is allowed except our one stylesheet (by hash) and form posts to ourselves.
pub fn csp() -> String {
    let hash = base64(&Sha256::digest(CSS.as_bytes()));
    format!(
        "default-src 'none'; style-src 'sha256-{hash}'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'"
    )
}

fn base64(bytes: &[u8]) -> String {
    const A: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::with_capacity(bytes.len().div_ceil(3) * 4);
    for chunk in bytes.chunks(3) {
        let b = [chunk[0], *chunk.get(1).unwrap_or(&0), *chunk.get(2).unwrap_or(&0)];
        let n = (u32::from(b[0]) << 16) | (u32::from(b[1]) << 8) | u32::from(b[2]);
        out.push(char::from(A[(n >> 18) as usize & 63]));
        out.push(char::from(A[(n >> 12) as usize & 63]));
        out.push(if chunk.len() > 1 { char::from(A[(n >> 6) as usize & 63]) } else { '=' });
        out.push(if chunk.len() > 2 { char::from(A[n as usize & 63]) } else { '=' });
    }
    out
}

/// A whole page: the shell around `body`. The title is text (escaped).
pub fn page(title: &str, body: Html) -> String {
    let head = Html::join([
        Html::lit("<meta charset=\"utf-8\">"),
        Html::lit("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"),
        Html::lit("<meta name=\"referrer\" content=\"no-referrer\">"),
        Html::tag("title", &[], Html::text(title)),
        Html::tag("style", &[], Html::lit(CSS)),
    ]);
    let main = Html::tag("main", &[], body);
    let mut out = String::from("<!doctype html>");
    out.push_str(
        Html::tag("html", &[("lang", "en")], Html::join([Html::tag("head", &[], head), Html::tag("body", &[], main)]))
            .as_str(),
    );
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_metacharacter_is_escaped() {
        assert_eq!(
            escape("<a href=\"x\" onclick='y'>&amp;</a>"),
            "&lt;a href=&quot;x&quot; onclick=&#39;y&#39;&gt;&amp;amp;&lt;/a&gt;"
        );
        assert_eq!(escape(""), "");
        assert_eq!(escape("plain text, 😀 é"), "plain text, 😀 é");
    }

    /// A small deterministic generator (no crate): xorshift.
    fn rng(state: &mut u64) -> u64 {
        *state ^= *state << 13;
        *state ^= *state >> 7;
        *state ^= *state << 17;
        *state
    }

    #[test]
    fn nothing_survives_escaping_that_could_open_markup_or_close_an_attribute() {
        let alphabet: Vec<char> = "<>\"'&/=;#abcxyz019 \n\t\u{202e}\u{0}é😀%".chars().collect();
        let mut s = 0x9E37_79B9_7F4A_7C15u64;
        for _ in 0..5000 {
            let len = (rng(&mut s) % 40) as usize;
            let raw: String = (0..len).map(|_| alphabet[(rng(&mut s) % alphabet.len() as u64) as usize]).collect();
            let out = escape(&raw);
            assert!(!out.contains(['<', '>', '"', '\'']), "{raw:?} -> {out:?}");
            // every & starts one of our entities and nothing else
            for (i, _) in out.match_indices('&') {
                let rest = &out[i..];
                assert!(
                    ["&amp;", "&lt;", "&gt;", "&quot;", "&#39;"].iter().any(|e| rest.starts_with(e)),
                    "{raw:?} -> {out:?}"
                );
            }
        }
    }

    #[test]
    fn tags_escape_their_attribute_values_and_their_text() {
        let h = Html::tag("a", &[("href", "/x?a=1&b=\"2\""), ("title", "'quoted'")], Html::text("<b>bold</b>"));
        assert_eq!(
            h.as_str(),
            "<a href=\"/x?a=1&amp;b=&quot;2&quot;\" title=\"&#39;quoted&#39;\">&lt;b&gt;bold&lt;/b&gt;</a>"
        );
        let i = Html::void("input", &[("value", "\"><script>")]);
        assert_eq!(i.as_str(), "<input value=\"&quot;&gt;&lt;script&gt;\">");
    }

    #[test]
    fn a_page_carries_a_title_as_text_and_no_script() {
        let p = page("</title><script>alert(1)</script>", Html::text("hi"));
        assert!(!p.contains("<script"));
        assert!(p.contains("&lt;/title&gt;"));
        assert!(p.starts_with("<!doctype html><html lang=\"en\">"));
    }

    #[test]
    fn base64_matches_the_known_vectors() {
        assert_eq!(base64(b""), "");
        assert_eq!(base64(b"f"), "Zg==");
        assert_eq!(base64(b"fo"), "Zm8=");
        assert_eq!(base64(b"foo"), "Zm9v");
        assert_eq!(base64(b"foobar"), "Zm9vYmFy");
    }

    #[test]
    fn the_csp_names_the_hash_of_the_stylesheet_and_allows_nothing_else() {
        let c = csp();
        assert!(c.starts_with("default-src 'none'; style-src 'sha256-"));
        assert!(c.ends_with("; form-action 'self'; base-uri 'none'; frame-ancestors 'none'"));
        assert!(!c.contains("script") && !c.contains("unsafe"));
        assert_eq!(c.matches("sha256-").count(), 1);
    }
}

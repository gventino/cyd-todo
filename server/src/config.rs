//! The configuration, from the environment (`docs/api.md`). A bad one stops the server at start
//! with a message that names the variable and never shows its value (the token is a secret).

use std::fmt;

use crate::limits::{PUBLIC_URL_MAX, TOKEN_MIN};

#[derive(Clone)]
pub struct Config {
    pub token: String,
    pub public_url: String,
    pub bind: String,
    pub database_url: String,
    pub tz_offset_min: i32,
    /// A fault-injection mode for tests of the firmware (A5). Only accepted with `ALLOW_FAULTS=1`.
    pub fault: Option<String>,
}

// The token must not end up in a log by a stray `{:?}`.
impl fmt::Debug for Config {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Config")
            .field("token", &"<hidden>")
            .field("public_url", &self.public_url)
            .field("bind", &self.bind)
            .field("database_url", &self.database_url)
            .field("tz_offset_min", &self.tz_offset_min)
            .field("fault", &self.fault)
            .finish()
    }
}

#[derive(Debug, PartialEq, Eq)]
pub struct ConfigError(pub String);

impl fmt::Display for ConfigError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

impl std::error::Error for ConfigError {}

impl Config {
    pub fn from_env() -> Result<Config, ConfigError> {
        Config::from_lookup(|k| std::env::var(k).ok())
    }

    /// `lookup` is the environment (a closure in the tests).
    pub fn from_lookup(lookup: impl Fn(&str) -> Option<String>) -> Result<Config, ConfigError> {
        let need = |name: &str| lookup(name).ok_or_else(|| ConfigError(format!("{name} is required")));

        let token = need("CYD_TOKEN")?;
        if token.len() < TOKEN_MIN {
            return Err(ConfigError(format!("CYD_TOKEN must be at least {TOKEN_MIN} characters")));
        }
        if token.chars().any(|c| c.is_control() || c.is_whitespace()) {
            return Err(ConfigError("CYD_TOKEN must not contain spaces or control characters".into()));
        }

        let public_url = need("PUBLIC_URL")?;
        if public_url.len() > PUBLIC_URL_MAX {
            return Err(ConfigError(format!(
                "PUBLIC_URL must be at most {PUBLIC_URL_MAX} bytes (it ends up in a QR code)"
            )));
        }
        if !(public_url.starts_with("http://") || public_url.starts_with("https://")) || public_url.ends_with('/') {
            return Err(ConfigError("PUBLIC_URL must look like http://host:port, without a trailing slash".into()));
        }
        if !public_url.is_ascii() || public_url.chars().any(|c| c.is_control() || c.is_whitespace()) {
            return Err(ConfigError("PUBLIC_URL must be plain ASCII".into()));
        }

        let tz_offset_min = match lookup("TZ_OFFSET_MIN") {
            None => 0,
            Some(v) => {
                let n: i32 = v
                    .trim()
                    .parse()
                    .map_err(|_| ConfigError("TZ_OFFSET_MIN must be a whole number of minutes".into()))?;
                if !(-14 * 60..=14 * 60).contains(&n) {
                    return Err(ConfigError("TZ_OFFSET_MIN must be between -840 and 840".into()));
                }
                n
            }
        };

        let fault = lookup("FAULT").filter(|f| !f.is_empty());
        if fault.is_some() && lookup("ALLOW_FAULTS").as_deref() != Some("1") {
            return Err(ConfigError("FAULT is only accepted together with ALLOW_FAULTS=1".into()));
        }
        if let Some(name) = fault.as_deref().filter(|n| crate::faults::Fault::parse(n).is_none()) {
            let _ = name;
            return Err(ConfigError(format!("FAULT must be one of: {}", crate::faults::Fault::names())));
        }

        Ok(Config {
            token,
            public_url,
            bind: lookup("BIND").unwrap_or_else(|| "0.0.0.0:8080".into()),
            database_url: lookup("DATABASE_URL").unwrap_or_else(|| "sqlite://cyd-todo.db".into()),
            tz_offset_min,
            fault,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;

    const TOKEN: &str = "0123456789abcdef0123456789abcdef";

    fn cfg(pairs: &[(&str, &str)]) -> Result<Config, ConfigError> {
        let m: HashMap<String, String> = pairs.iter().map(|(k, v)| (k.to_string(), v.to_string())).collect();
        Config::from_lookup(|k| m.get(k).cloned())
    }

    fn base() -> Vec<(&'static str, &'static str)> {
        vec![("CYD_TOKEN", TOKEN), ("PUBLIC_URL", "http://192.168.1.50:8080")]
    }

    #[test]
    fn defaults() {
        let c = cfg(&base()).unwrap();
        assert_eq!(c.bind, "0.0.0.0:8080");
        assert_eq!(c.database_url, "sqlite://cyd-todo.db");
        assert_eq!(c.tz_offset_min, 0);
        assert!(c.fault.is_none());
    }

    #[test]
    fn token_rules() {
        assert!(cfg(&[("PUBLIC_URL", "http://a:1")]).is_err());
        assert!(cfg(&[("CYD_TOKEN", &TOKEN[..31]), ("PUBLIC_URL", "http://a:1")]).is_err());
        assert!(cfg(&[("CYD_TOKEN", &TOKEN[..32]), ("PUBLIC_URL", "http://a:1")]).is_ok());
        let spaced = format!("{} {}", &TOKEN[..16], &TOKEN[16..]);
        assert!(cfg(&[("CYD_TOKEN", &spaced), ("PUBLIC_URL", "http://a:1")]).is_err());
    }

    #[test]
    fn an_error_never_shows_the_token() {
        let short = "short-secret-value";
        let e = cfg(&[("CYD_TOKEN", short), ("PUBLIC_URL", "http://a:1")]).unwrap_err();
        assert!(!e.to_string().contains(short));
        let c = cfg(&base()).unwrap();
        assert!(!format!("{c:?}").contains(TOKEN));
    }

    #[test]
    fn public_url_rules() {
        assert!(cfg(&[("CYD_TOKEN", TOKEN)]).is_err());
        let ok32 = "http://192.168.100.200:8080/xxxx"; // exactly 32 bytes, but ends in a letter
        assert_eq!(ok32.len(), 32);
        assert!(cfg(&[("CYD_TOKEN", TOKEN), ("PUBLIC_URL", ok32)]).is_ok());
        let long = format!("{ok32}x");
        assert!(cfg(&[("CYD_TOKEN", TOKEN), ("PUBLIC_URL", &long)]).is_err());
        assert!(cfg(&[("CYD_TOKEN", TOKEN), ("PUBLIC_URL", "ftp://host")]).is_err());
        assert!(cfg(&[("CYD_TOKEN", TOKEN), ("PUBLIC_URL", "http://host/")]).is_err());
        assert!(cfg(&[("CYD_TOKEN", TOKEN), ("PUBLIC_URL", "http://hóst")]).is_err());
    }

    #[test]
    fn time_zone_offset_and_faults() {
        let mut v = base();
        v.push(("TZ_OFFSET_MIN", "-180"));
        assert_eq!(cfg(&v).unwrap().tz_offset_min, -180);
        for bad in ["abc", "841", "-841", "1.5", ""] {
            let mut v = base();
            v.push(("TZ_OFFSET_MIN", bad));
            assert!(cfg(&v).is_err(), "{bad:?}");
        }
        let mut v = base();
        v.push(("FAULT", "slow"));
        assert!(cfg(&v).is_err());
        v.push(("ALLOW_FAULTS", "1"));
        assert_eq!(cfg(&v).unwrap().fault.as_deref(), Some("slow"));
        for (name, _) in crate::faults::Fault::ALL {
            let mut v = base();
            v.push(("FAULT", name));
            v.push(("ALLOW_FAULTS", "1"));
            assert!(cfg(&v).is_ok(), "{name}");
        }
        for bad in ["Slow", "nonsense", "slow ", "error 500"] {
            let mut v = base();
            v.push(("FAULT", bad));
            v.push(("ALLOW_FAULTS", "1"));
            let e = cfg(&v).unwrap_err().to_string();
            assert!(e.contains("slow") && e.contains("wrongtype"), "the message lists the modes: {e}");
        }
        let mut v = base();
        v.push(("FAULT", "slow"));
        v.push(("ALLOW_FAULTS", "yes"));
        assert!(cfg(&v).is_err(), "only the exact 1 allows faults");
    }
}

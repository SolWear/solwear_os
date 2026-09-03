//! `manifest.json` parsing and validation for `.swa` packages.

use serde::{Deserialize, Serialize};

/// Capability names recognised by the daemon. They map one to one onto the
/// method namespace prefix used in the JSON-RPC API.
pub const CAPABILITIES: &[&str] = &[
    "system",
    "power",
    "display",
    "sensors",
    "notifications",
    "apps",
    "wallet",
];

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct Manifest {
    pub id: String,
    pub name: String,
    pub version: String,
    pub sdk: String,
    #[serde(rename = "type")]
    pub app_type: String,
    pub entry: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub icon: Option<String>,
    #[serde(default)]
    pub capabilities: Vec<String>,
    #[serde(default)]
    pub author: String,
    #[serde(default)]
    pub description: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ManifestError(pub String);

impl std::fmt::Display for ManifestError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

impl std::error::Error for ManifestError {}

fn err(message: impl Into<String>) -> ManifestError {
    ManifestError(message.into())
}

impl Manifest {
    pub fn parse(bytes: &[u8]) -> Result<Manifest, ManifestError> {
        let manifest: Manifest = serde_json::from_slice(bytes)
            .map_err(|e| err(format!("manifest.json is not valid: {e}")))?;
        manifest.validate()?;
        Ok(manifest)
    }

    pub fn validate(&self) -> Result<(), ManifestError> {
        validate_app_id(&self.id)?;
        if self.name.trim().is_empty() {
            return Err(err("manifest `name` must not be empty"));
        }
        validate_version(&self.version)?;
        if self.sdk.trim().is_empty() {
            return Err(err("manifest `sdk` must not be empty"));
        }
        if !matches!(self.app_type.as_str(), "app" | "watchface") {
            return Err(err(format!(
                "manifest `type` must be `app` or `watchface`, got `{}`",
                self.app_type
            )));
        }
        validate_relative_path(&self.entry, "entry")?;
        if let Some(icon) = &self.icon {
            validate_relative_path(icon, "icon")?;
        }
        for capability in &self.capabilities {
            if !CAPABILITIES.contains(&capability.as_str()) {
                return Err(err(format!("unknown capability `{capability}`")));
            }
        }
        Ok(())
    }

    pub fn has_capability(&self, capability: &str) -> bool {
        self.capabilities.iter().any(|c| c == capability)
    }
}

/// App ids are reverse-DNS, lowercase, and immutable. Keeping the character set
/// tight means an id is always safe to use as a directory name and as a URL
/// path segment.
pub fn validate_app_id(id: &str) -> Result<(), ManifestError> {
    if id.is_empty() || id.len() > 128 {
        return Err(err("app id must be between 1 and 128 characters"));
    }
    if id == "tech.solwear.shell" {
        return Err(err("app id `tech.solwear.shell` is reserved by the system"));
    }
    let labels: Vec<&str> = id.split('.').collect();
    if labels.len() < 2 {
        return Err(err(format!(
            "app id `{id}` must be reverse-DNS, e.g. tech.solwear.app"
        )));
    }
    for label in labels {
        if label.is_empty() {
            return Err(err(format!("app id `{id}` has an empty label")));
        }
        if !label.chars().next().is_some_and(|c| c.is_ascii_lowercase()) {
            return Err(err(format!(
                "app id `{id}` has a label that does not start with a lowercase letter"
            )));
        }
        if !label
            .chars()
            .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == '-')
        {
            return Err(err(format!(
                "app id `{id}` may only contain lowercase letters, digits, hyphens and dots"
            )));
        }
    }
    Ok(())
}

/// Versions are `major.minor.patch` with an optional pre-release suffix.
fn validate_version(version: &str) -> Result<(), ManifestError> {
    let core = version.split(['-', '+']).next().unwrap_or("");
    let parts: Vec<&str> = core.split('.').collect();
    if parts.len() != 3
        || parts
            .iter()
            .any(|p| p.is_empty() || !p.chars().all(|c| c.is_ascii_digit()))
    {
        return Err(err(format!(
            "manifest `version` must be semver, got `{version}`"
        )));
    }
    Ok(())
}

/// Paths inside a package must stay inside the package.
pub fn validate_relative_path(path: &str, field: &str) -> Result<(), ManifestError> {
    if path.trim().is_empty() {
        return Err(err(format!("manifest `{field}` must not be empty")));
    }
    if path.starts_with('/') || path.starts_with('\\') || path.contains(':') {
        return Err(err(format!(
            "manifest `{field}` must be a relative path, got `{path}`"
        )));
    }
    if path
        .split(['/', '\\'])
        .any(|segment| segment == ".." || segment == ".")
    {
        return Err(err(format!(
            "manifest `{field}` must not traverse directories: `{path}`"
        )));
    }
    Ok(())
}

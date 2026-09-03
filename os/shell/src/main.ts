// Shell entry point.

import "./styles.css";
import { applyScreen } from "./layout.js";
import { Shell } from "./shell.js";

const root = document.getElementById("root");
if (!root) throw new Error("shell root element is missing");

// Start with a sensible default so the first frame is already laid out; the
// real geometry arrives from `system.info` a moment later.
applyScreen({ width: 480, height: 480, shape: "round" });

const shell = new Shell(root);
void shell.start();

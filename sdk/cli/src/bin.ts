#!/usr/bin/env node
/** The `solwear` executable. */

import { main } from "./index.js";

const code = await main(process.argv.slice(2));
if (code !== 0) process.exit(code);

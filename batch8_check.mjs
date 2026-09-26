// Batch 8 check: painting hover glow and the click-to-enlarge viewer.
import { chromium } from "playwright";

const URL = process.env.TEST_URL ?? "http://localhost:8000/museum.html?v=b8a";
const errors = [];
const browser = await chromium.launch({ channel: "msedge", headless: true });
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
page.on("console", (m) => { if (m.type() === "error") errors.push(m.text()); });
page.on("pageerror", (e) => errors.push("PAGEERROR: " + e.message));

await page.goto(URL, { waitUntil: "load", timeout: 90000 });
await page.waitForFunction(() => typeof _museumHoveredPainting === "function", { timeout: 90000 });
await page.click("#overlay");
await page.waitForTimeout(800);

// Stand 3.2m in front of a painting, facing it.
async function facePainting(index, name) {
  const spot = await page.evaluate((i) => [
    _museumPaintingX(i), _museumPaintingY(i), _museumPaintingZ(i), _museumPaintingRy(i),
  ], index);
  const [x, y, z, ry] = spot;
  const nx = Math.sin(ry), nz = Math.cos(ry);
  const dist = 3.2;
  const yaw = Math.atan2(nx, nz);
  await page.evaluate(
    ([px, py, pz, pyaw]) => _museumSetPose(px, py, pz, pyaw, 0.0),
    [x + nx * dist, y, z + nz * dist, yaw],
  );
  await page.waitForTimeout(500);
  const hovered = await page.evaluate(() => _museumHoveredPainting());
  const on = await page.evaluate(() => document.getElementById("crosshair").classList.contains("on"));
  console.log(`${name}: expected=${index} hovered=${hovered} crosshairOn=${on}`);
  await page.screenshot({ path: `batch8-hover-${name}.png` });
  return { hovered, on, index };
}

const checks = [];
const land = await facePainting(3, "landscape");
checks.push(["landscape hover", land.hovered === 3]);
const port = await facePainting(14, "portrait");
checks.push(["portrait hover", port.hovered === 14]);

// Aim into a wall: no painting under the crosshair.
await page.evaluate(() => _museumSetPose(-3.129, -1.75, -8.9, Math.PI / 2, 0.0));
await page.waitForTimeout(400);
const none = await page.evaluate(() => _museumHoveredPainting());
console.log(`wall: hovered=${none}`);
checks.push(["no hover on wall", none === -1]);

// Open the viewer on the portrait painting.
await facePainting(14, "portrait");
// Playwright's mouse cannot deliver a click while the pointer is locked, so
// dispatch on the canvas directly -- the same event the handler listens for.
await page.evaluate(() => document.getElementById("canvas").dispatchEvent(
  new MouseEvent("click", { bubbles: true })));
await page.waitForTimeout(600);
const open = await page.evaluate(() => document.getElementById("viewer").classList.contains("open"));
const src = await page.evaluate(() => document.getElementById("viewer-image").src);
console.log(`viewer open=${open} srcIsBlob=${src.startsWith("blob:")}`);
checks.push(["viewer opens", open === true]);
checks.push(["viewer image is a blob", src.startsWith("blob:")]);
await page.screenshot({ path: "batch8-viewer.png" });

// Escape closes it.
await page.keyboard.press("Escape");
await page.waitForTimeout(300);
const closed = await page.evaluate(() => !document.getElementById("viewer").classList.contains("open"));
checks.push(["escape closes viewer", closed === true]);

console.log("\nRESULTS");
let failed = 0;
for (const [n, ok] of checks) { console.log(` ${ok ? "ok  " : "FAIL"}  ${n}`); if (!ok) failed++; }
const real = errors.filter((e) => !e.includes("404"));
console.log("errors:", JSON.stringify(real));
if (real.length) failed++;
await browser.close();
console.log(failed === 0 ? "\nBATCH8 OK" : `\nBATCH8 FAILED (${failed})`);
process.exit(failed === 0 ? 0 : 1);

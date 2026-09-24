// Batch 2 verification: build counts + spawn/eye/collision/walk, mirroring the
// checks in the JS museum's smoke.mjs.
import { chromium } from "playwright";

const URL = process.env.TEST_URL ?? "http://localhost:8000/museum.html?v=b2b";
const errors = [];

const browser = await chromium.launch({ channel: "msedge", headless: true });
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
page.on("console", (m) => {
  if (m.type() === "error") errors.push(m.text());
});
page.on("pageerror", (e) => errors.push("PAGEERROR: " + e.message));

await page.goto(URL, { waitUntil: "load", timeout: 60000 });
await page.waitForFunction(() => typeof _museumRoomCount === "function", { timeout: 60000 });
await page.click("#overlay");
await page.waitForTimeout(1200);

const read = () =>
  page.evaluate(() => ({
    x: _museumPlayerX(), y: _museumPlayerY(), z: _museumPlayerZ(),
    eyeY: _museumEyeY(), onGround: _museumOnGround(),
  }));

const counts = await page.evaluate(() => ({
  rooms: _museumRoomCount(),
  segments: _museumSegmentCount(),
  colliders: _museumColliderCount(),
  paintingSpots: _museumPaintingSpotCount(),
}));

let start = await read();
const eyeAboveFeet = start.eyeY - (start.y - 1.1);
console.log("counts:", JSON.stringify(counts));
console.log("spawn:", JSON.stringify(start));
console.log("eye above feet:", eyeAboveFeet.toFixed(3));

const checks = [];
checks.push(["43 rooms", counts.rooms === 43]);
checks.push(["spawn x", Math.abs(start.x - -3.129) < 0.01]);
checks.push(["spawn z", Math.abs(start.z - -8.9) < 0.01]);
checks.push(["eye height 1.769", Math.abs(eyeAboveFeet - 1.769) < 0.01]);
checks.push(["on ground at spawn", start.onGround === 1]);

// Walk forward (-X, yaw is PI/2) for 0.8s
await page.keyboard.down("KeyW");
await page.waitForTimeout(800);
await page.keyboard.up("KeyW");
const afterWalk = await read();
console.log("after 0.8s walk:", JSON.stringify(afterWalk));
checks.push(["walked -X", afterWalk.x < start.x - 0.5]);

// Run at the wall for 3s: must stop inside the building, not clip through.
await page.keyboard.down("KeyW");
await page.waitForTimeout(3000);
await page.keyboard.up("KeyW");
const atWall = await read();
console.log("after 3.8s walk:", JSON.stringify(atWall));
checks.push(["no west-wall clip", atWall.x > -14.8]);
checks.push(["still on ground", atWall.onGround === 1]);

// Gravity: step off into the room, must stay supported (no fall-through).
await page.waitForTimeout(500);
const settled = await read();
checks.push(["y stable", Math.abs(settled.y - start.y) < 0.05]);

console.log("\nRESULTS");
let failed = 0;
for (const [name, ok] of checks) {
  console.log(` ${ok ? "ok  " : "FAIL"}  ${name}`);
  if (!ok) failed++;
}
// A 404 for a favicon is unrelated to the museum.
const realErrors = errors.filter((e) => !e.includes("404"));
console.log("errors:", JSON.stringify(realErrors));
if (realErrors.length > 0) failed++;

await browser.close();
console.log(failed === 0 ? "\nBATCH2 OK" : `\nBATCH2 FAILED (${failed})`);
process.exit(failed === 0 ? 0 : 1);

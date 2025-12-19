import { SceneManager } from "./scene/SceneManager";
import { Planet } from "./planet/Planet";

export class App {
  private sceneManager: SceneManager;
  private planet: Planet;

  constructor() {
    this.sceneManager = new SceneManager();
    this.planet = new Planet();
  }

  start() {
    this.sceneManager.scene.add(this.planet.object3D);
    this.sceneManager.start();
  }
}

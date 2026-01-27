import { Component, signal } from '@angular/core';
import { MulticastSettingsComponent } from './multicast-settings/multicast-settings';

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [MulticastSettingsComponent],
  template: `<app-multicast-settings />`,
  styleUrl: './app.css',
})
export class App {
  protected readonly title = signal('acap-angular-ui');
}


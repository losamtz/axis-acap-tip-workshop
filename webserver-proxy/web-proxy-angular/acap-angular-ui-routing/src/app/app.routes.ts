import { Routes } from '@angular/router';
import { MulticastSettingsComponent } from './multicast-settings/multicast-settings';

export const routes: Routes = [
  { path: '', component: MulticastSettingsComponent },
  { path: '**', redirectTo: '' },
];

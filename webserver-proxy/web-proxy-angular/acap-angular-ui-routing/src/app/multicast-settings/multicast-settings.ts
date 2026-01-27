import { Component, OnInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { HttpErrorResponse } from '@angular/common/http';

import { ApiService, InfoResponse, SaveResponse } from '../api';

@Component({
  selector: 'app-multicast-settings',
  standalone: true,
  imports: [CommonModule, FormsModule],
  templateUrl: './multicast-settings.html',
  styleUrls: ['./multicast-settings.css'],
})
export class MulticastSettingsComponent implements OnInit {
  addr: string = '';
  port: string = '';
  out: unknown = {};
  loading: boolean = false;
  saving: boolean = false;
  error: string | null = null;

  constructor(private readonly api: ApiService) {}

  ngOnInit(): void {
    this.loadInfo();
  }

  loadInfo(): void {
    this.loading = true;
    this.error = null;

    this.api.getInfo().subscribe({
      next: (json: InfoResponse): void => {
        this.out = json;

        if (typeof json.MulticastAddress === 'string' && json.MulticastAddress.trim().length > 0) {
          this.addr = json.MulticastAddress;
        }

        if (json.MulticastPort !== undefined && json.MulticastPort !== null) {
          this.port = String(json.MulticastPort);
        }
        
        this.loading = false;
      },
      error: (err: unknown): void => {
        this.error = `Failed to load info: ${this.formatError(err)}`;
        console.error(err);
        this.loading = false;
      },
    });
  }

  save(): void {
    this.saving = true;
    this.error = null;

    const body: { MulticastAddress: string; MulticastPort: string } = {
      MulticastAddress: this.addr.trim(),
      MulticastPort: this.port.trim(),
    };

    this.api.setParam(body).subscribe({
      next: (json: SaveResponse): void => {
        this.out = json;
      },
      error: (err: unknown): void => {
        this.error = `Failed to save parameters: ${this.formatError(err)}`;
        console.error(err);
      },
      complete: (): void => {
        this.saving = false;
      },
    });
  }

  jsonText(): string {
    return JSON.stringify(this.out ?? {}, null, 2);
  }

  private formatError(err: unknown): string {
    if (err instanceof HttpErrorResponse) {
      // Prefer backend-provided message/body if present
      const backend = err.error;
      if (typeof backend === 'string' && backend.trim().length > 0) return backend;
      if (backend && typeof backend === 'object') {
        try {
          return JSON.stringify(backend);
        } catch {
          /* ignore */
        }
      }
      return `${err.status} ${err.statusText || err.message}`.trim();
    }

    if (err instanceof Error) return err.message;

    if (typeof err === 'string') return err;

    try {
      return JSON.stringify(err);
    } catch {
      return 'Unknown error';
    }
  }
}

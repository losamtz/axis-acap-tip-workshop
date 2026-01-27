import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { Observable } from 'rxjs';

export interface InfoResponse {
  MulticastAddress?: string;
  MulticastPort?: string | number;
  [key: string]: unknown;
}

@Injectable({ providedIn: 'root' })
export class ApiService {
  private readonly BASE = '/local/web_proxy/api';

  constructor(private http: HttpClient) {}

  getInfo(): Observable<InfoResponse> {
    return this.http.get<InfoResponse>(`${this.BASE}/info`, {
      headers: { 'Cache-Control': 'no-cache' },
      withCredentials: true,
    });
  }

  setParam(body: { MulticastAddress: string; MulticastPort: string }) {
    return this.http.post(`${this.BASE}/param`, body, {
      headers: { 'Content-Type': 'application/json' },
      withCredentials: true,
    });
  }
}

export type SaveResponse = unknown;

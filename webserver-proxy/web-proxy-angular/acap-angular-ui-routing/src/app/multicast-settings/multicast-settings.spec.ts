import { ComponentFixture, TestBed } from '@angular/core/testing';

import { MulticastSettingsComponent } from './multicast-settings';

describe('MulticastSettingsComponent', () => {
  let component: MulticastSettingsComponent;
  let fixture: ComponentFixture<MulticastSettingsComponent>;
  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [MulticastSettingsComponent]
    })
    .compileComponents();

    fixture = TestBed.createComponent(MulticastSettingsComponent);
    component = fixture.componentInstance;
    await fixture.whenStable();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
